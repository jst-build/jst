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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_SETUP_LOG_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_SETUP_LOG_HPP

#include <cstddef>
#include <mutex>
#include <string>

#include "gsl/gsl"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"

/// \brief The setup log reports the progress of the setup phase by printing
/// one line for each repository that actually has to be set up, at the time
/// the work on it starts. Nothing is printed if the repositories being set up
/// are already reported interactively, but they are still counted. Each line
/// consists of an unpadded counter, i.e., the index of the repository and the
/// total number of repositories, followed by the name of the repository:
///
///    [ Counter ] Setting up [ Repository ]
///
/// Nothing is ever cut. Repositories that pre-existed, i.e., that are taken
/// from cache or require no work at all, are not printed, but still consume
/// their index, which leaves gaps in the counter:
///
///    [1/68] Setting up rules-cc
///    [2/68] Setting up protobuf
///    [5/68] Setting up grpc
///
/// Note that nothing is known yet about the work to be done at that point in
/// time; the amounts of data and the duration are only reported in the
/// summary, once the setup is complete.
class SetupLog {
  public:
    explicit SetupLog(gsl::not_null<JustMRProgress*> const& progress,
                      bool quiet = false) noexcept
        : progress_{progress}, quiet_{quiet} {}

    /// \brief Report a repository that work on has started, given its index
    /// among all repositories. Thread-safe.
    void NotifyStart(std::string const& repo, std::size_t index) noexcept;

    /// \brief Get count of actual setup work (non-cached repositories).
    [[nodiscard]] auto GetWorkCount() const noexcept -> std::size_t;

  private:
    gsl::not_null<JustMRProgress*> progress_;
    bool quiet_;
    mutable std::mutex mutex_;
    std::size_t count_{};
};

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_SETUP_LOG_HPP
