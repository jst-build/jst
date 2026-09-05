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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_HPP

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/progress_reporting/task_tracker.hpp"

/// \brief The progress class is a wrapper for the task tracker and the origin
/// map. The task tracker is used to get dynamic information about the currently
/// running actions during the execution phase. The origin map contains static
/// information about the origins of the actions (e.g., the targets and the
/// action number within these targets) and is populated after the analysis
/// phase and before the execution phase. Progress reporting uses the dynamic
/// information from the task tracker to get a snapshot of the currently running
/// actions and uses the static information from the origin map to supplement
/// action information with target information.
class Progress {
  public:
    [[nodiscard]] auto TaskTracker() noexcept -> TaskTracker& {
        return task_tracker_;
    }

    // Return a reference to the origin map. It is the responsibility of the
    // caller to ensure that access only happens in a single-threaded context.
    [[nodiscard]] auto OriginMap() noexcept
        -> std::unordered_map<
            std::string,
            std::vector<
                std::pair<BuildMaps::Target::ConfiguredTarget, std::size_t>>>& {
        return origin_map_;
    }

    // Return a reference to the label map. It is the responsibility of the
    // caller to ensure that access only happens in a single-threaded context.
    [[nodiscard]] auto LabelMap() noexcept
        -> std::unordered_map<std::string, std::string>& {
        return label_map_;
    }

    // Set the hook that is called whenever an action is started to be
    // executed or, for actions served from cache, once they are done. It is
    // the responsibility of the caller to set the hook before the execution
    // phase starts, as it is read from multiple threads during execution, and
    // to ensure that the hook itself is thread-safe and does not throw.
    void SetActionStartHook(
        std::function<void(std::string const&, bool)> hook) {
        action_start_hook_ = std::move(hook);
    }

    // Notify about the start of an action, reporting whether the action is
    // actually executed or was served from cache. Thread-safe, given that the
    // hook itself is thread-safe. The hook must not throw.
    void NotifyActionStart(std::string const& action_id,
                           bool executed) const noexcept {
        if (action_start_hook_) {
            action_start_hook_(action_id, executed);
        }
    }

  private:
    ::TaskTracker task_tracker_{};
    std::unordered_map<
        std::string,
        std::vector<
            std::pair<BuildMaps::Target::ConfiguredTarget, std::size_t>>>
        origin_map_;
    std::unordered_map<std::string, std::string> label_map_;
    std::function<void(std::string const&, bool)> action_start_hook_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_HPP
