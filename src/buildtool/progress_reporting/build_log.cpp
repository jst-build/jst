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

#include "src/buildtool/progress_reporting/build_log.hpp"

#include <exception>
#include <string>

#include "fmt/format.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/progress_reporting/progress_style.hpp"
#include "src/buildtool/progress_reporting/task_string.hpp"

void BuildLog::NotifyStart(std::string const& action_id,
                           bool executed) noexcept {
    try {
        auto const total = progress_->OriginMap().size();
        auto const label = TaskLabel(progress_, action_id);
        auto const origin = TaskOrigin(progress_, action_id);

        std::lock_guard lock{mutex_};
        auto const count = ++count_;
        if (not executed) {
            // actions served from cache are counted, but not reported
            return;
        }
        Logger::LogRaw(logger_, LogLevel::Progress, [&](bool colored) {
            ProgressStyle const style{colored};
            auto const counter = fmt::format("[{}/{}]", count, total);
            // the label column is padded to its width, but never cut
            auto const label_width =
                kLabelColumnWidth > counter.size() + 1
                    ? kLabelColumnWidth - counter.size() - 1
                    : 0;
            return fmt::format(
                "{} {}{}{}",
                style.Green(counter),
                style.Blue(fmt::format("{:<{}}", label, label_width)),
                kColumnSeparator,
                origin);
        });
    } catch (std::exception const& ex) {
        Logger::Log(logger_,
                    LogLevel::Warning,
                    "writing the build log failed with:\n{}",
                    ex.what());
        // continue with the build
    }
}
