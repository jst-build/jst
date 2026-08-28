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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_PAGER_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_PAGER_HPP

#include <csignal>
#include <cstdio>

/// \brief Redirects standard output to a pager for as long as this object is
/// alive, so that long output can be browsed comfortably. Paging happens only
/// if standard output is attached to a terminal; otherwise, and if anything
/// goes wrong while setting up the pager, standard output is left alone.
///
/// The pager is taken from the environment variable JST_PAGER, or PAGER,
/// defaulting to "less" if neither is set. Setting either of them to the empty
/// string disables paging. Unless the environment already says otherwise, less
/// is asked to quit if the output fits on a single screen (and hence to not
/// page short output at all) and to pass colors through.
class Pager {
  public:
    explicit Pager(bool enabled) noexcept;
    ~Pager() noexcept;
    Pager(Pager const&) = delete;
    Pager(Pager&&) = delete;
    auto operator=(Pager const&) -> Pager& = delete;
    auto operator=(Pager&&) -> Pager& = delete;

  private:
    std::FILE* pager_{nullptr};
    int saved_stdout_{-1};
    // NOLINTNEXTLINE(misc-include-cleaner)
    void (*saved_sigpipe_)(int){SIG_DFL};
};

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_PAGER_HPP
