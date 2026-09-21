// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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

#include "src/buildtool/progress_reporting/dynamic_progress_reporter.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fmt/format.h"
#include "src/buildtool/progress_reporting/progress_style.hpp"
#include "src/buildtool/system/terminal.hpp"

namespace {

// The dynamic progress reporter constantly updates its printed information
// about the current build process. This requires volatile log messages, which
// are deleted if new information is going to be printed. Every line of the
// report is laid out in the same three columns:
//
//    [ Label Column ] 2xSpace [ Duration Column ] 2xSpace [ Origin Column ]
//
// There is one line for each of the longest-running actions (at most
// kMaxTasks), holding a spinner and the label of the action, the time the
// action is running already, and its origin, i.e., the target name plus the
// action number. If even more actions are running, this is stated in an
// additional line. The bottom line holds a progress bar in terms of executed
// or cached actions, the time the build is running already, and statistics
// about the actions. For a terminal width of 72, the report may look as
// follows:
//
//     ⠼ Compiling task_system.cpp                   0.3s  '...:task_system'#0
//     ⠼ Compiling builtin_functions.cpp           11m21s  '...ast:builtins'#0
//     ⠼ Patching file options.h                     0.4s  '...se/options.h'#0
//       ... and 16 more
//        Building ━━╸───────────────────────────    0.5s  34/382 done.
//
// For the widths of the columns and how they adapt to the width of the
// terminal, see the constants below.
class DynamicProgressReporterImpl {
  private:
    static auto constexpr kMaxTasks = 8;

    // Label column: " " + spinner + " " + action label. It is left-aligned
    // and exceeding content is cut on the right, ending with "...". In the
    // bottom line, it holds the progress bar instead.
    static auto constexpr kLabelMaxWidth = 43U;
    static auto constexpr kLabelMinWidth = 23U;
    // Prefix of the label in the label column: " " + spinner + " "
    static auto constexpr kLabelPrefixWidth = 3U;
    // Prefix of the line reporting further running actions
    static constexpr std::string_view kMoreActionsPrefix = "   ";
    // Prefix of the progress bar in the bottom line
    static constexpr std::string_view kBottomLinePrefix = "    Building ";

    // Duration column: kDurationWidth wide, right-aligned, never cut.

    // Origin column: quoted target name + action number. It is left-aligned
    // and it has no maximum width. Exceeding content is shortened in two
    // steps: first the module of the target is cut on the left, keeping
    // repository, target, and action number ('repo//module:target'#NN
    // becomes 'repo//...dule:target'#NN), and only if that does not suffice,
    // the whole origin is cut on the left ('xxxxx'#NN becomes '...xx'#NN). In
    // the bottom line, it holds the statistics, from which "processing" and
    // then "cached" are dropped if they do not fit, but "done" is always shown.
    // If the terminal is too narrow, the origin column shrinks first, and only
    // once it reached its minimum width, the label column shrinks as well.
    static auto constexpr kOriginMinWidth = 17U;

    static const std::string kThreeDots;

    struct State {
        int cached;
        int run;
        int queued;
        std::vector<std::string> samples;
    };

    struct ColumnWidths {
        std::size_t label{};
        std::size_t origin{};
    };

  public:
    explicit DynamicProgressReporterImpl(
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        Logger const* logger)
        : stats_{stats}, progress_{progress}, logger_{logger} {}

    auto operator()() -> void {
        // Redraw on every call, even if nothing changed, so that the report is
        // updated at a constant rate.
        // Note: order matters; queued has to be queried last
        state_ = {.cached = stats_->ActionsCachedCounter(),
                  .run = stats_->ActionsExecutedCounter(),
                  .queued = stats_->ActionsQueuedCounter(),
                  .samples = progress_->TaskTracker().Sample(kMaxTasks)};

        // determine progress parameters
        auto const total = gsl::narrow<int>(progress_->OriginMap().size());
        auto const num_samples = gsl::narrow<int>(state_.samples.size());
        auto const done = state_.run + state_.cached;
        auto const active = state_.queued - state_.run - state_.cached;
        auto const widths = DetermineColumnWidths();

        Logger::LogVolatile(
            logger_, LogLevel::Progress, [&, this](bool colored) {
                ProgressStyle const style{colored};
                // print a line for each currently running task
                std::string progress_message{};
                for (auto const& sample : state_.samples) {
                    progress_message +=
                        TaskString(sample, widths, style) + "\n";
                }
                if (num_samples > 0 and active > num_samples) {
                    progress_message += fmt::format("{}... and {} more\n",
                                                    kMoreActionsPrefix,
                                                    active - num_samples);
                }

                // print bottom line
                progress_message +=
                    BottomLineString(done, total, active, widths, style);

                return progress_message;
            });
        ++frame_;
    }

  private:
    gsl::not_null<Statistics*> stats_;
    gsl::not_null<Progress*> progress_;
    Logger const* logger_;
    // the reporter is created when the build starts
    std::chrono::steady_clock::time_point start_{
        std::chrono::steady_clock::now()};
    std::size_t frame_{};
    State state_{};

    /// \brief Determine the widths of the label and the origin column for the
    /// current width of the terminal. The origin column takes all the space
    /// not needed by the label column, and it is the first to shrink.
    [[nodiscard]] static auto DetermineColumnWidths() -> ColumnWidths {
        auto const width =
            std::size_t{Terminal::Width().value_or(kDefaultTerminalWidth)};
        // the duration column and the separators around it
        auto const fixed = kDurationWidth + (2 * kColumnSeparator.size());
        auto const available = width > fixed ? width - fixed : 0;
        if (available >= kLabelMaxWidth + kOriginMinWidth) {
            return {.label = kLabelMaxWidth,
                    .origin = available - kLabelMaxWidth};
        }
        auto const label = available > kOriginMinWidth + kLabelMinWidth
                               ? available - kOriginMinWidth
                               : std::size_t{kLabelMinWidth};
        return {.label = label, .origin = kOriginMinWidth};
    }

    /// \brief Compose a line from the contents of its three columns, which
    /// have to be of the respective widths already.
    [[nodiscard]] static auto Line(std::string const& label,
                                   std::string const& duration,
                                   std::string const& origin) -> std::string {
        return fmt::format("{}{}{}{}{}",
                           label,
                           kColumnSeparator,
                           duration,
                           kColumnSeparator,
                           origin);
    }

    /// \brief The content of the duration column, dimmed.
    [[nodiscard]] static auto DurationString(
        std::chrono::steady_clock::duration duration,
        ProgressStyle const& style) -> std::string {
        return style.Dim(
            fmt::format("{:>{}}", FormatDuration(duration), kDurationWidth));
    }

    [[nodiscard]] auto OriginString(std::string const& sample,
                                    std::size_t max_width) -> std::string {
        std::string result{};
        auto const& origin_map = progress_->OriginMap();
        auto origins = origin_map.find(sample);
        if (origins != origin_map.end() and not origins->second.empty()) {
            auto const& origin = origins->second[0];
            result = fmt::format(
                "{}#{}", origin.first.target.ToString(), origin.second);
        }
        else {
            result = sample;
        }
        if (result.size() <= max_width) {
            return result;
        }
        // the module is the least interesting part, so it is cut first
        if (auto const module = ModulePos(result)) {
            auto const rest = result.size() - module->second + module->first;
            if (rest + kThreeDots.size() <= max_width) {
                auto const keep = max_width - rest - kThreeDots.size();
                return result.substr(0, module->first) + kThreeDots +
                       result.substr(module->second - keep);
            }
        }
        return CutOnLeft(result, max_width);
    }

    /// \brief Obtain where the module of an origin of the form
    /// 'repo//module:target'#NN starts and ends, if it has that form. The
    /// repository and the module are JSON strings if they contain separating
    /// characters themselves, which is taken into account.
    [[nodiscard]] static auto ModulePos(std::string const& origin)
        -> std::optional<std::pair<std::size_t, std::size_t>> {
        if (not origin.starts_with('\'')) {
            return std::nullopt;
        }
        // the repository is separated from the module by "//", the module
        // from the target by ":"; both may contain any other character
        auto const repo_end = SegmentEnd(origin, 1, "//");
        if (not repo_end) {
            return std::nullopt;
        }
        auto const start = *repo_end + 2;
        auto const module_end = SegmentEnd(origin, start, ":");
        if (not module_end) {
            return std::nullopt;
        }
        return std::pair{start, *module_end};
    }

    /// \brief Obtain the position of the given separator ending the segment
    /// of an origin that starts at the given position. A segment is a JSON
    /// string if it contains separating characters itself.
    [[nodiscard]] static auto SegmentEnd(std::string const& origin,
                                         std::size_t start,
                                         std::string_view separator)
        -> std::optional<std::size_t> {
        if (start >= origin.size()) {
            return std::nullopt;
        }
        auto end = start;
        if (origin[start] == '"') {
            // skip the JSON string, in which quotes are escaped
            end = std::string::npos;
            for (auto pos = start + 1; pos < origin.size(); ++pos) {
                if (origin[pos] == '\\') {
                    ++pos;
                }
                else if (origin[pos] == '"') {
                    end = pos + 1;
                    break;
                }
            }
        }
        else {
            end = origin.find(separator, start);
        }
        if (end == std::string::npos or
            origin.compare(end, separator.size(), separator) != 0) {
            return std::nullopt;
        }
        return end;
    }

    /// \brief Cut an origin on the left to fit the given width, keeping the
    /// character it starts with, e.g., 'xxxxx'#NN becomes '...xx'#NN.
    [[nodiscard]] static auto CutOnLeft(std::string const& origin,
                                        std::size_t max_width) -> std::string {
        std::string mark{};
        switch (origin[0]) {
            case '[':
                mark = "[";
                break;
            case '\'':
                mark = "'";
                break;
            default:
                break;
        }
        if (max_width <= mark.size() + kThreeDots.size()) {
            return origin.substr(origin.size() - max_width);
        }
        return mark + kThreeDots +
               origin.substr(origin.size() - max_width + mark.size() +
                             kThreeDots.size());
    }

    [[nodiscard]] auto LabelString(std::string const& sample) -> std::string {
        std::string result{};
        if (progress_->TaskTracker().IsUploading(sample)) {
            result = "Uploading";
        }
        else {
            auto const& label_map = progress_->LabelMap();
            auto label = label_map.find(sample);
            if (label != label_map.end()) {
                result = label->second;
            }
            else {
                result = "Executing";
            }
        }
        return result;
    }

    /// \brief The label of an action, padded to the given width or cut on the
    /// right to fit it, ending with "..." then.
    [[nodiscard]] auto LabelString(std::string const& sample,
                                   std::size_t width) -> std::string {
        auto label = LabelString(sample);
        if (label.size() > width) {
            label =
                width > kThreeDots.size()
                    ? label.substr(0, width - kThreeDots.size()) + kThreeDots
                    : label.substr(0, width);
        }
        return fmt::format("{:<{}}", label, width);
    }

    [[nodiscard]] auto TaskString(std::string const& sample,
                                  ColumnWidths const& widths,
                                  ProgressStyle const& style) -> std::string {
        auto const label_width = widths.label > kLabelPrefixWidth
                                     ? widths.label - kLabelPrefixWidth
                                     : 0;
        return Line(
            fmt::format(" {} {}",
                        Spinner(frame_),
                        style.Blue(LabelString(sample, label_width))),
            DurationString(progress_->TaskTracker().Duration(sample), style),
            OriginString(sample, widths.origin));
    }

    /// \brief The statistics about the actions, of which the components about
    /// cached and processing actions are dropped if they do not fit.
    [[nodiscard]] auto SummaryString(int done,
                                     int total,
                                     int active,
                                     std::size_t max_width) -> std::string {
        auto summary = fmt::format("{}/{} done, {} cached, {} processing.",
                                   done,
                                   total,
                                   state_.cached,
                                   active);
        if (summary.size() > max_width) {
            summary = fmt::format(
                "{}/{} done, {} cached.", done, total, state_.cached);
        }
        if (summary.size() > max_width) {
            summary = fmt::format("{}/{} done.", done, total);
        }
        return summary;
    }

    [[nodiscard]] auto BottomLineString(int done,
                                        int total,
                                        int active,
                                        ColumnWidths const& widths,
                                        ProgressStyle const& style)
        -> std::string {
        auto const bar_width = widths.label > kBottomLinePrefix.size()
                                   ? widths.label - kBottomLinePrefix.size()
                                   : 0;
        return Line(
            fmt::format("{}{}",
                        style.Green(std::string{kBottomLinePrefix}),
                        style.Bar(static_cast<double>(done) / total,
                                  static_cast<unsigned int>(bar_width))),
            DurationString(std::chrono::steady_clock::now() - start_, style),
            SummaryString(done, total, active, widths.origin));
    }
};

const std::string DynamicProgressReporterImpl::kThreeDots{"..."};

}  // namespace

auto DynamicProgressReporter::Reporter(gsl::not_null<Statistics*> const& stats,
                                       gsl::not_null<Progress*> const& progress,
                                       Logger const* logger) noexcept
    -> progress_reporter_t {
    return BaseProgressReporter::Reporter(
        DynamicProgressReporterImpl{stats, progress, logger}, kDefaultPeriod);
}
