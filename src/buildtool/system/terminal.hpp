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

#ifndef INCLUDED_SRC_BUILDTOOL_SYSTEM_TERMINAL_HPP
#define INCLUDED_SRC_BUILDTOOL_SYSTEM_TERMINAL_HPP

#include <cstdlib>
#include <optional>

#include <sys/ioctl.h>
#include <unistd.h>

/// \brief Properties of the terminal we are attached to and the user's
/// preferences about colored output.
namespace Terminal {

/// \brief Check whether an environment variable is set to a non-empty value.
[[nodiscard]] static inline auto IsEnvSet(char const* name) noexcept -> bool {
    char const* value = std::getenv(name);
    return value != nullptr and *value != '\0';
}

/// \brief Check whether stderr is attached to a terminal.
[[nodiscard]] static inline auto IsTty() noexcept -> bool {
    return isatty(STDERR_FILENO) == 1;
}

/// \brief Obtain the width of the terminal attached to stderr, if any. Note
/// that a terminal may as well report a width of zero, e.g., if it has been
/// allocated without specifying a window size. In this case the width is
/// reported as unknown.
[[nodiscard]] static inline auto Width() noexcept
    -> std::optional<unsigned int> {
    struct winsize ws {};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == -1 or ws.ws_col == 0) {
        return std::nullopt;
    }
    return ws.ws_col;
}

/// \brief Check whether we are running interactively, that is, stderr is
/// attached to a terminal and we are not running in a CI/CD environment.
[[nodiscard]] static inline auto IsInteractive() noexcept -> bool {
    return IsTty() and not IsEnvSet("CI");
}

/// \brief Decide whether colored output should be used for the given file
/// descriptor, stderr by default, if the user did not explicitly ask for or
/// against colors. The environment variables FORCE_COLOR (only ever enables
/// colors) and NO_COLOR (see https://no-color.org) are honored, in this
/// order, before falling back to terminal detection.
[[nodiscard]] static inline auto UseColor() noexcept -> bool {
    if (IsEnvSet("FORCE_COLOR")) {
        return true;
    }
    if (IsEnvSet("NO_COLOR")) {
        return false;
    }
    return IsTty();
}

}  // namespace Terminal

#endif  // INCLUDED_SRC_BUILDTOOL_SYSTEM_TERMINAL_HPP
