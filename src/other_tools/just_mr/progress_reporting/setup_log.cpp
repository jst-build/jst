// Copyright 2026 The jst-build authors.
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

#include "src/other_tools/just_mr/progress_reporting/setup_log.hpp"

#include <exception>

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress_style.hpp"

void SetupLog::NotifyStart(std::string const& repo,
                           std::size_t index) noexcept {
    try {
        auto const total = progress_->GetTotal();

        std::lock_guard lock{mutex_};
        ++count_;
        if (quiet_) {
            // the repositories are reported interactively instead
            return;
        }
        Logger::LogRaw(nullptr, LogLevel::Progress, [&](bool colored) {
            ProgressStyle const style{colored};
            return fmt::format(
                "{} {}",
                style.Green(fmt::format("[{}/{}]", index, total)),
                style.Blue(fmt::format("Setting up {}", repo)));
        });
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Warning,
                    "writing the setup log failed with:\n{}",
                    ex.what());
        // continue with the setup
    }
}

auto SetupLog::GetWorkCount() const noexcept -> std::size_t {
    std::lock_guard lock{mutex_};
    return count_;
}