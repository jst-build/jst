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
#include <string_view>

#include "fmt/color.h"
#include "fmt/format.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink.hpp"
#include "src/buildtool/logging/logger.hpp"

// Escape-code sequence to avoid partial redraw on a VT100 terminal
//  - \033[?2026h and \033[?2026l begin and end a synchronized update, i.e.,
//    the terminal does not display anything in between, but the result as a
//    whole; terminals not supporting this mode simply ignore it
constexpr std::string_view kBeginSynchronizedUpdate = "\033[?2026h";
constexpr std::string_view kEndSynchronizedUpdate = "\033[?2026l";
// Escape-code sequences to redraw a volatile message in place on a VT100
// terminal, so that in the worst case some lines may be displayed blank, but
// never the entire block:
//  - \033[<N>A moves the cursor up N rows, to the first row of the message, and
//    \r to the start of that row
//  - \033[K clears the row from the cursor to its end, i.e., what is left of
//    the previous content to the right of a new line
//  - \033[J clears everything below the cursor, i.e., the remaining rows of a
//    previous message that was taller
constexpr std::string_view kClearToEndOfRow = "\033[K";
constexpr std::string_view kClearBelow = "\033[J";

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

    /// \brief Thread-safe emitting of styled log messages to stderr, which
    /// are colored if this sink is set up to do so.
    void Emit(Logger const* logger,
              LogLevel level,
              StyledMessage const& msg,
              MessageStyle style) const noexcept final {
        Emit(logger, level, msg(colored_), style);
    }

    /// \brief Thread-safe emitting of log messages to stderr.
    void Emit(Logger const* logger,
              LogLevel level,
              std::string const& msg,
              MessageStyle style) const noexcept final {
        static std::mutex mutex{};

        if (restrict_level_ and
            (static_cast<int>(*restrict_level_) < static_cast<int>(level))) {
            return;
        }

        bool const clear_next = style == MessageStyle::Volatile;
        bool const prefixed = style == MessageStyle::Normal;

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
            static std::size_t num_clear_lines{};
            bool const clear = num_clear_lines > 0;
            if (clear) {
                fmt::print(stderr, "{}", kBeginSynchronizedUpdate);
                // move curser up N rows and just overwrite existing output
                fmt::print(stderr, "\033[{}A\r", num_clear_lines);
            }
            num_clear_lines = clear_next ? num_lines : 0;
            if (msg_on_continuation and prefixed) {
                if (clear) {
                    fmt::print(stderr, "{}", kClearToEndOfRow);
                }
                fmt::print(stderr, "{}\n", prefix);
                prefix = cont_prefix;
            }
            using it = std::istream_iterator<ILogSink::Line>;
            std::istringstream iss{msg};
            for_each(it{iss}, it{}, [&](auto const& line) {
                if (clear) {
                    // Unconditionally clear the entire row (in case the line to
                    // overwrite is longer than the new content).
                    // Q: Why cannot we just clear the remaining row after the
                    //    new line was written?
                    // A: If the line length fits exactly the terminal width,
                    //    depending on the terminal (xterm, ghostty, but not
                    //    tmux), the curser position will stay at the last
                    //    column. In that case, the last char will be cleared
                    //    as well in those terminals.
                    fmt::print(stderr, "{}", kClearToEndOfRow);
                }
                if (prefixed) {
                    fmt::print(stderr, "{} ", prefix);
                    prefix = cont_prefix;
                }
                fmt::print(stderr, "{}\n", line);
            });
            if (clear) {
                // unconditionally clear everything below
                fmt::print(stderr, "{}", kClearBelow);
                fmt::print(stderr, "{}", kEndSynchronizedUpdate);
            }
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
