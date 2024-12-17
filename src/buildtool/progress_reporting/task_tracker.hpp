// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_TRACKER_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_TRACKER_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

/// \brief The task tracker class keeps track of the currently running action
/// executions of a build. Actions are registered at the task tracker when they
/// are started and deregistered when they are finished (called by execution
/// threads). Futhermore, the task tracker allows to query the oldest still
/// running sample from this transient collection of tasks, which is then used
/// in progress reporting (called by progress reporter thread).
///
/// Implementation considerations: since we want to avoid putting unnecessary
/// processing effort to the action execution threads, a hashmap-based approach
/// is used, where adding and removing a task is cheap. Determining the oldest
/// still running task is offloaded to the progress reporter thread, which is
/// not performance critical.
///
/// Each task gets assigned a prio value, which is basically the current counter
/// value of all registered tasks so far. It is incremented each time a new task
/// is registered. This means, to find the oldest still running task one needs
/// to determine the task with the smallest prio value.
class TaskTracker {
  public:
    auto Start(const std::string& id) noexcept -> void {
        std::unique_lock lock(m_);
        ++prio_;
        try {
            running_.emplace(id, prio_);
        } catch (...) {
            Logger::Log(LogLevel::Warning,
                        "Internal error in progress tracking; progress reports "
                        "might be incorrect.");
        }
    }

    auto Stop(const std::string& id) noexcept -> void {
        std::unique_lock lock(m_);
        running_.erase(id);
    }

    [[nodiscard]] auto Sample() noexcept -> std::string {
        std::unique_lock lock(m_);
        std::string result{};
        std::uint64_t started = prio_ + 1;
        for (auto const& it : running_) {
            if (it.second < started) {
                result = it.first;
                started = it.second;
            }
        }
        return result;
    }

    [[nodiscard]] auto Sample(int n) noexcept -> std::vector<std::string> {
        std::unique_lock lock(m_);

        // Determine the n most long-running tasks (the n tasks with the
        // smallest prio values). A max heap of size n is used to store the n
        // tasks with the smallest prio values.
        std::vector<std::string> tasks{};
        tasks.reserve(n);

        // Fill the heap.
        auto it = running_.cbegin();
        for (int i{}; i < n && it != running_.cend(); ++i, ++it) {
            tasks.push_back(it->first);
        }

        // Establish max heap property. The default behavior of the
        // std::make_heap function is to create a max heap if the comparison
        // function returns true if the left argument is smaller than the right
        // argument ('less than' comparison function). The first element of the
        // heap will contain the task with the largest prio value.
        auto cmp = [&running = running_](std::string const& a,
                                         std::string const& b) {
            return running[a] <= running[b];
        };
        std::make_heap(tasks.begin(), tasks.end(), cmp);

        // Scan the remaining tasks for tasks with smaller prio values than the
        // max value in the heap and replace them with the max value from the
        // heap.
        for (; it != running_.cend(); ++it) {
            auto const& task = tasks.front();
            if (it->second < running_[task]) {
                std::pop_heap(tasks.begin(), tasks.end(), cmp);
                tasks.back() = it->first;
                std::push_heap(tasks.begin(), tasks.end(), cmp);
            }
        }

        // Sort heap vector, results in increasing order with a 'less than'
        // comparison function.
        std::sort_heap(tasks.begin(), tasks.end(), cmp);
        return tasks;
    }

    [[nodiscard]] auto Active() noexcept -> std::size_t {
        std::unique_lock lock(m_);
        return running_.size();
    }

  private:
    std::uint64_t prio_{};
    std::mutex m_;
    std::unordered_map<std::string, std::uint64_t> running_;
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_TRACKER_HPP
