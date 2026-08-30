// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/// \brief The kind of work currently done for a repository.
enum class SetupPhase : std::uint8_t {
    // Downloading from the network, fetching from a remote git repository, or
    // collecting the content of a distribution directory
    kFetching,
    // Extracting an archive
    kUnpacking,
    // Importing to git or to the local CAS
    kImporting,
    // Running a command or building a computed root
    kComputing
};

/// \brief Progress of an ongoing transfer, e.g., a download. The total amount
/// to transfer is not always known beforehand.
struct TransferProgress {
    std::uint64_t current{};
    std::optional<std::uint64_t> total{};
};

/// \brief Progress of a single repository.
struct RepoProgress {
    SetupPhase phase{};
    std::chrono::steady_clock::time_point start{};
    // order in which the repositories started to be worked on
    std::uint64_t prio{};
    // whether work is currently being done for this repository
    bool active{};
    // transfer of the current phase, in bytes and, for git fetches, also in
    // objects; both are reset whenever a new phase is started
    TransferProgress bytes{};
    TransferProgress objects{};
};

/// \brief Tracks what is currently done for each repository to be set up, as
/// well as the overall progress. Thread-safe, as the repositories are set up
/// concurrently.
class JustMRProgress final {
  public:
    /// \brief Hook called once work on a repository starts, which only
    /// happens for repositories that actually have to be set up. It is given
    /// the name of the repository and its index among all repositories, in the
    /// order they started to be worked on or were found to be set up already.
    /// It is called with the progress locked, so that the indices are
    /// reported in order; hence it must not call back into the progress other
    /// than for its total. Must be thread-safe and must not throw.
    using StartHook =
        std::function<void(std::string const&, std::size_t index)>;

    explicit JustMRProgress(std::size_t total) noexcept : total_{total} {};

    [[nodiscard]] auto GetTotal() const noexcept -> std::size_t {
        return total_;
    }

    /// \brief Report that work on a repository has started. Calls the hook,
    /// if any is set, the first time work on this repository starts.
    void Start(std::string const& repo, SetupPhase phase) noexcept {
        std::lock_guard lock{mutex_};
        auto& entry = repos_[repo];
        if (not entry.active) {
            entry.prio = ++prio_;
            entry.active = true;
            if (entry.start == std::chrono::steady_clock::time_point{}) {
                entry.start = std::chrono::steady_clock::now();
                ++counted_;
                if (start_hook_) {
                    start_hook_(repo, counted_);
                }
            }
        }
        entry.phase = phase;
    }

    /// \brief Report that the kind of work done for a repository has changed.
    void SetPhase(std::string const& repo, SetupPhase phase) noexcept {
        std::lock_guard lock{mutex_};
        if (auto entry = repos_.find(repo); entry != repos_.end()) {
            entry->second.phase = phase;
            entry->second.bytes = {};
            entry->second.objects = {};
        }
    }

    /// \brief Report the number of bytes transferred over the network for a
    /// repository so far in the current phase, and the total number of bytes
    /// to transfer, if known. A transfer that starts over, e.g., because a
    /// mirror is tried, is counted from the beginning again.
    void SetBytes(std::string const& repo,
                  std::uint64_t bytes,
                  std::optional<std::uint64_t> total = std::nullopt) noexcept {
        std::lock_guard lock{mutex_};
        auto& entry = repos_[repo];
        // a transfer that starts over reports fewer bytes than before
        auto const delta =
            bytes >= entry.bytes.current ? bytes - entry.bytes.current : bytes;
        entry.bytes.current = bytes;
        fetched_ += delta;
        // the overall goal only ever grows, as it is a lower bound of what is
        // still unknown; hence, only take note of an increased total
        if (total and *total > entry.bytes.total.value_or(0)) {
            goal_ += *total - entry.bytes.total.value_or(0);
            entry.bytes.total = total;
        }
    }

    /// \brief Report the number of git objects received for a repository so
    /// far in the current phase, out of the total number to receive.
    void SetObjects(std::string const& repo,
                    std::uint64_t objects,
                    std::uint64_t total) noexcept {
        std::lock_guard lock{mutex_};
        auto& entry = repos_[repo];
        entry.objects.current = objects;
        entry.objects.total = total;
    }

    /// \brief Report that work on a repository has stopped, at least for now.
    void Stop(std::string const& repo) noexcept {
        std::lock_guard lock{mutex_};
        if (auto entry = repos_.find(repo); entry != repos_.end()) {
            entry->second.active = false;
        }
    }

    /// \brief Report that a repository is set up. A repository that was
    /// never worked on is counted here, as it was set up already.
    void Done(std::string const& repo) noexcept {
        std::lock_guard lock{mutex_};
        auto entry = repos_.find(repo);
        if (entry != repos_.end()) {
            entry->second.active = false;
        }
        // work on a repository might have been recorded without it ever being
        // started, so check for its start time rather than for its entry
        if (entry == repos_.end() or
            entry->second.start == std::chrono::steady_clock::time_point{}) {
            ++counted_;
        }
        ++done_;
    }

    /// \brief Obtain for how long the repositories are being set up already.
    [[nodiscard]] auto GetDuration() const noexcept
        -> std::chrono::steady_clock::duration {
        return std::chrono::steady_clock::now() - start_;
    }

    /// \brief Obtain the number of bytes fetched from the network in total.
    [[nodiscard]] auto GetFetched() const noexcept -> std::uint64_t {
        std::lock_guard lock{mutex_};
        return fetched_;
    }

    /// \brief Obtain the number of bytes known to be fetched from the network
    /// in total. As sizes are only discovered while setting up, this is a
    /// lower bound that grows over time.
    [[nodiscard]] auto GetGoal() const noexcept -> std::uint64_t {
        std::lock_guard lock{mutex_};
        return goal_;
    }

    /// \brief Obtain the number of repositories that are set up.
    [[nodiscard]] auto GetDone() const noexcept -> std::size_t {
        std::lock_guard lock{mutex_};
        return done_;
    }

    /// \brief Obtain the repositories currently being worked on, oldest first,
    /// and the total number of such repositories.
    [[nodiscard]] auto Sample(std::size_t max) const noexcept
        -> std::pair<std::vector<std::pair<std::string, RepoProgress>>,
                     std::size_t> {
        std::lock_guard lock{mutex_};
        std::vector<std::pair<std::string, RepoProgress>> active{};
        for (auto const& [repo, entry] : repos_) {
            if (entry.active) {
                active.emplace_back(repo, entry);
            }
        }
        auto const size = active.size();
        std::sort(
            active.begin(), active.end(), [](auto const& l, auto const& r) {
                return l.second.prio < r.second.prio;
            });
        if (active.size() > max) {
            active.resize(max);
        }
        return {std::move(active), size};
    }

    /// \brief Set the hook to be called once work on a repository starts.
    /// Must be set before the setup starts, as it is read from multiple
    /// threads.
    void SetStartHook(StartHook hook) { start_hook_ = std::move(hook); }

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, RepoProgress> repos_;
    std::uint64_t prio_{};
    std::uint64_t fetched_{};
    std::uint64_t goal_{};
    // number of repositories set up, including the ones that pre-existed
    std::size_t done_{};
    // number of repositories started to be worked on or found to pre-exist
    std::size_t counted_{};
    std::size_t total_{};
    // the progress is created when the setup starts
    std::chrono::steady_clock::time_point start_{
        std::chrono::steady_clock::now()};
    StartHook start_hook_{};
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_PROGRESS_HPP
