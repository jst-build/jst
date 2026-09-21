// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/buildtool/progress_reporting/base_progress_reporter.hpp"

#include <chrono>
#include <exception>
#include <mutex>
#include <type_traits>
#include <utility>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace {
void CallReport(std::function<void(void)> const& report) noexcept {
    try {
        report();
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "calling progress report function failed with:\n{}",
                    ex.what());
        // continue with progress reporting
    }
}
}  // namespace

auto BaseProgressReporter::Reporter(std::function<void(void)> report,
                                    std::int64_t interval) noexcept
    -> progress_reporter_t {
    return [report = std::move(report), interval](std::atomic<bool>* done,
                                                  std::condition_variable* cv) {
        std::mutex m;
        std::unique_lock<std::mutex> lock(m);
        auto const period = std::chrono::milliseconds{interval};
        auto next = std::chrono::steady_clock::now() + period;
        while (not *done) {
            // wait for the next activation; the predicate rules out spurious
            // wake-ups, which would otherwise cause additional reports
            if (cv->wait_until(lock, next, [done] { return done->load(); })) {
                break;
            }
            CallReport(report);
            // activations are due at fixed points in time, independent of how
            // long the report takes; activations missed while reporting are
            // skipped rather than caught up in a burst
            next += period;
            if (auto const now = std::chrono::steady_clock::now();
                next <= now) {
                next += ((now - next) / period + 1) * period;
            }
        }
        // Call the reporter a final time to print the latest state.
        CallReport(report);
    };
}
