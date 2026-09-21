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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_STYLE_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_STYLE_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

#include "fmt/color.h"
#include "fmt/format.h"
#include "src/buildtool/system/terminal.hpp"

// Layout shared by all progress reports, see
// doc/concepts/progress-reporting.md for how they are composed.

/// \brief Separator between two columns of any progress report.
inline constexpr std::string_view kColumnSeparator = "  ";

/// \brief Width of the terminal assumed if it cannot be determined.
inline constexpr auto kDefaultTerminalWidth = 80U;

/// \brief Width of a column holding a duration, which fits any duration below
/// 24 hours, from "0.0s" up to "23h59m". Longer durations are not cut, but
/// exceed their column.
inline constexpr auto kDurationWidth = 6U;

/// \brief Obtain a human-readable duration: with a single decimal place below
/// a minute ("3.1s"), in minutes and seconds below an hour ("2m03s"), and in
/// hours and minutes beyond ("1h07m").
[[nodiscard]] static inline auto FormatDuration(
    std::chrono::steady_clock::duration duration) -> std::string {
    auto const millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    static constexpr auto kMillisPerSecond = 1000;
    static constexpr auto kSecondsPerMinute = 60;
    static constexpr auto kMinutesPerHour = 60;
    static constexpr auto kSecondsPerHour = kSecondsPerMinute * kMinutesPerHour;
    static constexpr auto kMillisPerDecimal = kMillisPerSecond / 10;
    auto const seconds = millis / kMillisPerSecond;
    if (seconds < kSecondsPerMinute) {
        return fmt::format(
            "{}.{}s", seconds, (millis % kMillisPerSecond) / kMillisPerDecimal);
    }
    if (seconds < kSecondsPerHour) {
        return fmt::format("{}m{:02}s",
                           seconds / kSecondsPerMinute,
                           seconds % kSecondsPerMinute);
    }
    return fmt::format("{}h{:02}m",
                       seconds / kSecondsPerHour,
                       (seconds / kSecondsPerMinute) % kMinutesPerHour);
}

/// \brief Obtain the frame of a spinner indicating ongoing work. Braille
/// patterns are used if the terminal can display them. Every frame is a single
/// character wide on the terminal.
[[nodiscard]] static inline auto Spinner(std::size_t frame) -> std::string {
    static constexpr std::array kUnicodeFrames{
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    static constexpr std::array kAsciiFrames{"|", "/", "-", "\\"};
    if (Terminal::SupportsUnicode()) {
        return kUnicodeFrames.at(frame % kUnicodeFrames.size());
    }
    return kAsciiFrames.at(frame % kAsciiFrames.size());
}

/// \brief Styling of progress reporting: the overall status is highlighted in
/// green, the names of the tasks being worked on in blue, secondary
/// information, such as durations, is dimmed, and everything else is left
/// unstyled.
class ProgressStyle {
  public:
    explicit ProgressStyle(bool colored) noexcept : colored_{colored} {}

    [[nodiscard]] auto Green(std::string const& msg) const -> std::string {
        return Apply(fg(fmt::color::lime_green), msg);
    }

    [[nodiscard]] auto Blue(std::string const& msg) const -> std::string {
        return Apply(fg(fmt::color::light_blue), msg);
    }

    [[nodiscard]] auto Dim(std::string const& msg) const -> std::string {
        return Apply(fmt::text_style{fmt::emphasis::faint}, msg);
    }

    /// \brief Render a progress bar of the given total width in columns,
    /// filled up to the given fraction of it. If the terminal can display
    /// them, Unicode characters are used, e.g., "━━━━━━╸───", otherwise ASCII
    /// characters, e.g., "[====>   ]".
    [[nodiscard]] auto Bar(double fraction,
                           unsigned int width) const -> std::string {
        // the comparison also rejects a fraction that is not a number
        auto const clamped = fraction > 0.0 ? std::min(fraction, 1.0) : 0.0;
        if (Terminal::SupportsUnicode()) {
            return UnicodeBar(clamped, width);
        }
        return AsciiBar(clamped, width);
    }

  private:
    bool colored_;

    [[nodiscard]] auto Apply(fmt::text_style const& style,
                             std::string const& msg) const -> std::string {
        return colored_ ? fmt::format(style, "{}", msg) : msg;
    }

    [[nodiscard]] static auto AsciiBar(double fraction,
                                       unsigned int width) -> std::string {
        static constexpr auto kBody = '=';
        static constexpr auto kTip = '>';
        static constexpr auto kEmpty = ' ';
        // the brackets are part of the given width
        auto const inner = width > 2 ? width - 2 : 0;
        auto const filled = static_cast<unsigned int>(fraction * inner);
        std::string result{"["};
        if (filled > 1) {
            result += std::string(filled - 1, kBody);
        }
        if (filled > 0) {
            result += std::string{kTip};
        }
        result += std::string(inner - filled, kEmpty);
        result += "]";
        return result;
    }

    /// \brief Repeat a string, e.g., a character of multiple bytes.
    [[nodiscard]] static auto Repeat(std::string_view text,
                                     unsigned int count) -> std::string {
        std::string result{};
        result.reserve(text.size() * count);
        for (unsigned int i{}; i < count; ++i) {
            result += text;
        }
        return result;
    }

    /// \brief A heavy line for the part done, ending in a half heavy line for
    /// half a column, and a light line for the rest; the part done is green
    /// and the rest dimmed, if colored.
    [[nodiscard]] auto UnicodeBar(double fraction,
                                  unsigned int width) const -> std::string {
        static constexpr std::string_view kDone = "━";
        static constexpr std::string_view kHalf = "╸";
        static constexpr std::string_view kToDo = "─";
        static constexpr auto kStepsPerColumn = 2U;
        auto const steps =
            static_cast<unsigned int>(fraction * kStepsPerColumn * width);
        auto const full = steps / kStepsPerColumn;
        auto const half = steps % kStepsPerColumn;
        auto done = Repeat(kDone, full);
        if (half > 0) {
            done += kHalf;
        }
        auto const todo = Repeat(kToDo, width - full - half);
        return (done.empty() ? done : Green(done)) +
               (todo.empty() ? todo : Dim(todo));
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_PROGRESS_STYLE_HPP
