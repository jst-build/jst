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

#include "src/buildtool/main/pager.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include <unistd.h>

#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/system/terminal.hpp"

namespace {

// Pager used if the environment does not specify one.
auto constexpr kDefaultPager = "less";

// Options for less: quit if the output fits on one screen, keep the output on
// screen after quitting, and pass color escape sequences through.
auto constexpr kDefaultLessOptions = "FRX";

/// \brief Check whether the given command can be found in PATH.
[[nodiscard]] auto IsAvailable(std::string const& command) noexcept -> bool {
    try {
        char const* const paths = std::getenv("PATH");
        if (paths == nullptr) {
            return false;
        }
        std::string const path_list{paths};
        std::size_t start{};
        while (start <= path_list.size()) {
            auto const end = path_list.find(':', start);
            auto const dir = path_list.substr(
                start,
                end == std::string::npos ? std::string::npos : end - start);
            if (not dir.empty() and
                access((std::filesystem::path{dir} / command).c_str(), X_OK) ==
                    0) {
                return true;
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Debug,
                    "Searching for pager {} failed with:\n{}",
                    command,
                    ex.what());
    }
    return false;
}

/// \brief Determine the pager to use, if any.
[[nodiscard]] auto PagerCommand() noexcept -> std::optional<std::string> {
    try {
        for (auto const* variable : {"JST_PAGER", "PAGER"}) {
            if (char const* const value = std::getenv(variable);
                value != nullptr) {
                std::string command{value};
                if (command.empty()) {
                    return std::nullopt;  // paging explicitly disabled
                }
                return command;
            }
        }
        if (IsAvailable(kDefaultPager)) {
            return std::string{kDefaultPager};
        }
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Debug,
                    "Determining the pager failed with:\n{}",
                    ex.what());
    }
    return std::nullopt;
}

}  // namespace

Pager::Pager(bool enabled) noexcept {
    if (not enabled or not Terminal::IsTty(STDOUT_FILENO)) {
        return;
    }
    auto const command = PagerCommand();
    if (not command) {
        return;
    }
    // honor the options already set for the pager, if any
    setenv("LESS", kDefaultLessOptions, /*overwrite=*/0);

    std::cout.flush();
    // NOLINTNEXTLINE(cert-env33-c)
    auto* pager = popen(command->c_str(), "w");
    if (pager == nullptr) {
        return;
    }
    saved_stdout_ = dup(STDOUT_FILENO);
    if (saved_stdout_ == -1 or dup2(fileno(pager), STDOUT_FILENO) == -1) {
        pclose(pager);
        return;
    }
    // the pager may be quit before all output has been read; writing to the
    // closed pipe must not terminate us
    saved_sigpipe_ = std::signal(SIGPIPE, SIG_IGN);
    pager_ = pager;
}

Pager::~Pager() noexcept {
    if (pager_ == nullptr) {
        return;
    }
    std::cout.flush();
    // restore standard output before waiting for the pager to finish
    dup2(saved_stdout_, STDOUT_FILENO);
    close(saved_stdout_);
    pclose(pager_);
    std::signal(SIGPIPE, saved_sigpipe_);
}
