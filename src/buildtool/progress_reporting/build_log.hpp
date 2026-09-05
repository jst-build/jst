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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BUILD_LOG_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BUILD_LOG_HPP

#include <cstddef>
#include <mutex>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress.hpp"

/// \brief The build log reports the progress of the build process by printing
/// one line for each action that is actually executed, at the time the
/// execution of the action is initiated. Every line is laid out in two
/// columns:
///
///    [ Label Column ] 2xSpace [ Origin Column ]
///
/// The label column holds a counter of the processed actions out of the total
/// number of actions, followed by the label of the action; the origin column
/// holds the target name plus the action number. Actions that were served
/// from cache are counted, but not printed, which leaves gaps in the sequence
/// of reported counter values. Content exceeding its column is never cut, but
/// merely breaks the alignment of the following column, e.g.:
///
///    [1/3558] Compiling os.cc                              'fmt//:fmt'#1
///    [2/3558] Compiling uncompr.c                          'zlib//:zlib'#13
///    [5/3558] Patching file options.h                      'absl//:options'#0
///    [123/3558] Compiling chacha20_poly1305_x86_64-linux.S  'ssl//:asm'#114
///
/// In contrast to the dynamic progress reporter, no information is ever
/// overwritten or truncated, which makes it the appropriate choice for
/// non-interactive use, such as writing to a log file.
class BuildLog {
  public:
    explicit BuildLog(gsl::not_null<Progress*> const& progress,
                      Logger const* logger = nullptr) noexcept
        : progress_{progress}, logger_{logger} {}

    /// \brief Report the start of an action, which is printed only if the
    /// action is actually executed. Thread-safe.
    void NotifyStart(std::string const& action_id, bool executed) noexcept;

  private:
    // Label column: counter + " " + action label, left-aligned and padded to
    // a fixed width, which fits a counter of up to "[1000/9999]" followed by
    // an action label of up to 40 characters. It is never cut.
    static auto constexpr kCounterMaxWidth = 11U;
    static auto constexpr kLabelMaxWidth = 40U;
    static auto constexpr kLabelColumnWidth =
        kCounterMaxWidth + 1 + kLabelMaxWidth;
    // Origin column: target name + action number, without any width limit.

    gsl::not_null<Progress*> progress_;
    Logger const* logger_;
    std::mutex mutex_;
    std::size_t count_{};
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BUILD_LOG_HPP
