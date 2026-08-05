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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>

#include "fmt/base.h"
#include "fmt/color.h"
#include "fmt/format.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"
#include "src/buildtool/logging/logger.hpp"

// Escape-code sequence to clear previous line on a VT100 terminal
//  - \033[A moves cursor up one line
//  - \r brings cursor to the beginning of the line
//  - \033[K clears line from cursor to the end
#ifdef __unix__
constexpr auto kClearLineCmd = "\033[A\r\033[K";
#endif

class LogSinkCmdLine final : public ILogSink {
  public:
    static auto CreateFactory(bool colored = true,
                              std::optional<LogLevel> restrict_level =
                                  std::nullopt) -> LogSinkFactory {
        return [=]() {
            return std::make_shared<LogSinkCmdLine>(colored, restrict_level);
        };
    }

    explicit LogSinkCmdLine(bool colored,
                            std::optional<LogLevel> restrict_level) noexcept
        : colored_{colored}, restrict_level_{restrict_level} {}
    ~LogSinkCmdLine() noexcept final = default;
    LogSinkCmdLine(LogSinkCmdLine const&) noexcept = delete;
    LogSinkCmdLine(LogSinkCmdLine&&) noexcept = delete;
    auto operator=(LogSinkCmdLine const&) noexcept -> LogSinkCmdLine& = delete;
    auto operator=(LogSinkCmdLine&&) noexcept -> LogSinkCmdLine& = delete;

    /// \brief Thread-safe emitting of log messages to stderr.
    void Emit(Logger const* logger,
              LogLevel level,
              std::string const& msg,
              bool clear) const noexcept final {
        static std::mutex mutex{};

        if (restrict_level_ and
            (static_cast<int>(*restrict_level_) < static_cast<int>(level))) {
            return;
        }

        auto prefix = LogLevelToString(level);

        if (logger != nullptr) {
            // append logger name
            prefix = fmt::format("{} ({})", prefix, logger->Name());
        }
        prefix = prefix + ":";
        auto cont_prefix = std::string(prefix.size(), ' ');
        prefix = FormatPrefix(level, prefix);
        bool msg_on_continuation{false};
        auto num_lines = static_cast<std::size_t>(
            1 + std::count(msg.begin(), msg.end(), '\n'));
        if (logger != nullptr and num_lines > 1) {
            cont_prefix = "    ";
            msg_on_continuation = true;
        }

        {
            std::lock_guard lock{mutex};
#ifdef __unix__
            static std::size_t num_clear_lines{};
            if (num_clear_lines > 0) {
                std::string clear_str{};
                clear_str.reserve(num_clear_lines * std::strlen(kClearLineCmd));
                for (std::size_t i{}; i < num_clear_lines; ++i) {
                    clear_str.append(kClearLineCmd);
                }
                fmt::print(stderr, "{}", clear_str);
            }
            num_clear_lines = clear ? num_lines : 0;
#endif
            if (msg_on_continuation and not clear) {
                fmt::print(stderr, "{}\n", prefix);
                prefix = cont_prefix;
            }
            using it = std::istream_iterator<ILogSink::Line>;
            std::istringstream iss{msg};
            for_each(it{iss}, it{}, [&](auto const& line) {
                if (not clear) {
                    fmt::print(stderr, "{} ", prefix);
                }
                fmt::print(stderr, "{}\n", line);
                prefix = cont_prefix;
            });
            std::fflush(stderr);
        }
    }

  private:
    bool colored_{};
    std::optional<LogLevel> restrict_level_;

    [[nodiscard]] auto FormatPrefix(LogLevel level, std::string const& prefix)
        const noexcept -> std::string {
        fmt::text_style style{};
        if (colored_) {
            switch (level) {
                case LogLevel::Error:
                    style = fg(fmt::color::red);
                    break;
                case LogLevel::Warning:
                    style = fg(fmt::color::orange);
                    break;
                case LogLevel::Info:
                case LogLevel::Verbose:
                    style = fg(fmt::color::lime_green);
                    break;
                case LogLevel::Progress:
                    style = fg(fmt::color::dark_green);
                    break;
                case LogLevel::Performance:
                    style = fg(fmt::color::light_sky_blue);
                    break;
                case LogLevel::Debug:
                    style = fg(fmt::color::sky_blue);
                    break;
                case LogLevel::Trace:
                    style = fg(fmt::color::deep_sky_blue);
                    break;
            }
        }
        try {
            return fmt::format(style, "{}", prefix);
        } catch (...) {
            return prefix;
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_CMDLINE_HPP
