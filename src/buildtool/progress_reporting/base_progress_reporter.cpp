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
                                    std::int64_t start_delay,
                                    double backoff_factor) noexcept
    -> progress_reporter_t {
    return [report = std::move(report), start_delay, backoff_factor](
               std::atomic<bool>* done, std::condition_variable* cv) {
        std::mutex m;
        std::unique_lock<std::mutex> lock(m);
        std::int64_t delay = start_delay;
        while (not *done) {
            cv->wait_for(lock, std::chrono::milliseconds(delay));
            if (not *done) {
                CallReport(report);
            }
            delay = static_cast<std::int64_t>(static_cast<double>(delay) *
                                              backoff_factor);
        }
        // Call the reporter a final time to print the latest state.
        CallReport(report);
    };
}
