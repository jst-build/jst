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

#include "src/other_tools/just_mr/progress_reporting/setup_reporter.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fmt/core.h"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/progress_reporting/progress_style.hpp"
#include "src/buildtool/system/terminal.hpp"
#include "src/other_tools/just_mr/progress_reporting/setup_style.hpp"

namespace {

// The setup reporter constantly updates its printed information about the
// repositories currently being set up. This requires volatile log messages,
// which are deleted if new information is going to be printed. Every line of
// the report is laid out in the same three columns:
//
//    [ Repository ] 2xSpace [ Duration ] 2xSpace [ Operation ]
//
// There is one line for each of the repositories worked on the longest (at
// most kMaxRepos), holding a spinner and the name of the repository, the time
// the repository is being worked on already, and the kind of work currently
// done for it, together with the progress of any transfer. If even more
// repositories are being worked on, this is stated in an additional line. The
// bottom line holds a progress bar in terms of repositories set up, including
// the ones that pre-existed, the time the setup is running already, and
// statistics about the repositories and the data fetched. For a terminal width
// of 72, the report may look as follows (with amounts cut that do not fit):
//
//     ⠼ re2/config                     0.5s  fetching ━━━━━───── 332.4 KiB
//     ⠼ absl                           1.2s  fetching ╸─────────
//     ⠼ protobuf                     11m21s  fetching ━━━━━╸────
//     ⠼ curl/config                    3.1s  unpacking
//     ⠼ libgit2                        0.1s  fetching
//       ... and 16 more
//        Setting up ━━━━━━━────────  11m21s  34/68 done, fetched 10.7 MiB
//
// For the widths of the columns and how they adapt to the width of the
// terminal, see the constants below.
class SetupReporterImpl {
  public:
    explicit SetupReporterImpl(
        gsl::not_null<JustMRProgress*> const& progress) noexcept
        : progress_{progress} {}

    auto operator()() -> void {
        auto const sampled = progress_->Sample(kMaxRepos);
        if (not started_ and sampled.second == 0) {
            // stay silent until work on a repository has started, so that
            // nothing is reported if all repositories pre-exist
            return;
        }
        started_ = true;
        Logger::LogVolatile(LogLevel::Progress, [&, this](bool colored) {
            return RenderMessage(
                sampled.first, sampled.second, ProgressStyle{colored});
        });
        ++frame_;
    }

  private:
    static auto constexpr kMaxRepos = std::size_t{8};

    // Repository column: " " + spinner + " " + repository name. It is
    // left-aligned and exceeding content is cut on the right. In the bottom
    // line, it holds the progress bar instead.
    static auto constexpr kRepositoryMaxWidth = 30U;
    static auto constexpr kRepositoryMinWidth = 20U;
    // Prefix of the name in the repository column: " " + spinner + " "
    static auto constexpr kRepositoryPrefixWidth = 3U;
    // Prefix of the line reporting further repositories being worked on
    static constexpr std::string_view kMoreReposPrefix = "   ";
    // Prefix of the progress bar in the bottom line
    static constexpr std::string_view kBottomLinePrefix = "    Setting up ";

    // Duration column: kDurationWidth wide, right-aligned, never cut.

    // Operation column: the kind of work done for the repository (at most
    // kPhaseMaxWidth chars) + " " + progress bar of the transfer (kBarWidth
    // chars, if the amount to transfer is known) + " " + the amount
    // transferred. It is left-aligned and exceeding content is cut on the
    // right, where the amount is dropped as a whole if it does not fit, while
    // phase and bar always fit the minimum width; it has no maximum width. In
    // the bottom line, it holds the statistics, of which the total amount to
    // fetch and then the amount fetched are dropped if they do not fit, but
    // the number of repositories done is always shown. If the terminal is too
    // narrow, the operation column shrinks first, and only once it reached its
    // minimum width, the repository column shrinks as well.
    static auto constexpr kPhaseMaxWidth = 9U;
    static auto constexpr kBarWidth = 10U;
    static auto constexpr kOperationMinWidth = 20U;

    struct ColumnWidths {
        std::size_t repository{};
        std::size_t operation{};
    };

    gsl::not_null<JustMRProgress*> progress_;
    std::size_t frame_{};
    bool started_{};

    /// \brief Render the report of the repositories currently being set up
    /// in the given style.
    [[nodiscard]] auto RenderMessage(
        std::vector<std::pair<std::string, RepoProgress>> const& sample,
        std::size_t active,
        ProgressStyle const& style) -> std::string {
        auto const widths = DetermineColumnWidths();

        std::string message{};
        for (auto const& [repo, entry] : sample) {
            message += RepoString(repo, entry, widths, style) + "\n";
        }
        if (active > sample.size()) {
            message += fmt::format("{}... and {} more\n",
                                   kMoreReposPrefix,
                                   active - sample.size());
        }
        message += BottomLineString(widths, style);
        return message;
    }

    /// \brief Determine the widths of the repository and the operation column
    /// for the current width of the terminal. The operation column takes all
    /// the space not needed by the repository column, and it is the first to
    /// shrink.
    [[nodiscard]] static auto DetermineColumnWidths() -> ColumnWidths {
        auto const width =
            std::size_t{Terminal::Width().value_or(kDefaultTerminalWidth)};
        // the duration column and the separators around it
        auto const fixed = kDurationWidth + (2 * kColumnSeparator.size());
        auto const available = width > fixed ? width - fixed : 0;
        if (available >= kRepositoryMaxWidth + kOperationMinWidth) {
            return {.repository = kRepositoryMaxWidth,
                    .operation = available - kRepositoryMaxWidth};
        }
        auto const repository =
            available > kOperationMinWidth + kRepositoryMinWidth
                ? available - kOperationMinWidth
                : std::size_t{kRepositoryMinWidth};
        return {.repository = repository, .operation = kOperationMinWidth};
    }

    /// \brief Compose a line from the contents of its three columns, which
    /// have to be of the respective widths already.
    [[nodiscard]] static auto Line(std::string const& repository,
                                   std::string const& duration,
                                   std::string const& operation)
        -> std::string {
        return fmt::format("{}{}{}{}{}",
                           repository,
                           kColumnSeparator,
                           duration,
                           kColumnSeparator,
                           operation);
    }

    /// \brief The content of the duration column, dimmed.
    [[nodiscard]] static auto DurationString(
        std::chrono::steady_clock::duration duration,
        ProgressStyle const& style) -> std::string {
        return style.Dim(
            fmt::format("{:>{}}", FormatDuration(duration), kDurationWidth));
    }

    /// \brief The kind of work done for a repository and the progress of its
    /// transfer, if any data has been transferred yet, cut on the right to fit
    /// the given width. A progress bar is shown whenever the amount to transfer
    /// is known; for git fetches, this is the number of objects, while the
    /// amount of data is reported as a number.
    [[nodiscard]] static auto OperationString(RepoProgress const& entry,
                                              std::size_t width,
                                              ProgressStyle const& style)
        -> std::string {
        auto operation = ToString(entry.phase).substr(0, kPhaseMaxWidth);
        // the number of columns the operation occupies on the terminal, as
        // the progress bar might consist of characters of multiple bytes
        auto columns = operation.size();
        if (entry.bytes.current > 0) {
            auto fraction = Fraction(entry.bytes);
            if (not fraction) {
                fraction = Fraction(entry.objects);
            }
            if (fraction) {
                operation +=
                    fmt::format(" {}", style.Bar(*fraction, kBarWidth));
                columns += 1 + kBarWidth;
            }
            auto const amount =
                entry.bytes.total
                    ? fmt::format("{}/{}",
                                  FormatBytes(entry.bytes.current),
                                  FormatBytes(*entry.bytes.total))
                    : FormatBytes(entry.bytes.current);
            // the amount is only reported as a whole
            if (columns + 1 + amount.size() <= width) {
                operation += fmt::format(" {}", amount);
                columns += 1 + amount.size();
            }
        }
        // phase and bar always fit the minimum width, so only a phase
        // without a bar might need to be cut
        return columns > width ? operation.substr(0, width) : operation;
    }

    /// \brief The fraction of a transfer that is done, if its total is known.
    [[nodiscard]] static auto Fraction(TransferProgress const& transfer)
        -> std::optional<double> {
        if (not transfer.total or *transfer.total == 0) {
            return std::nullopt;
        }
        return static_cast<double>(transfer.current) /
               static_cast<double>(*transfer.total);
    }

    /// \brief One line per repository: its name, for how long it is being
    /// worked on, and what is done for it.
    [[nodiscard]] auto RepoString(std::string const& repo,
                                  RepoProgress const& entry,
                                  ColumnWidths const& widths,
                                  ProgressStyle const& style) const
        -> std::string {
        auto const name_width = widths.repository > kRepositoryPrefixWidth
                                    ? widths.repository - kRepositoryPrefixWidth
                                    : 0;
        return Line(
            fmt::format(" {} {}",
                        Spinner(frame_),
                        style.Blue(fmt::format(
                            "{:<{}}", repo.substr(0, name_width), name_width))),
            DurationString(std::chrono::steady_clock::now() - entry.start,
                           style),
            OperationString(entry, widths.operation, style));
    }

    /// \brief The bottom line, summarizing the overall progress.
    [[nodiscard]] auto BottomLineString(ColumnWidths const& widths,
                                        ProgressStyle const& style) const
        -> std::string {
        auto const done = progress_->GetDone();
        auto const total = progress_->GetTotal();
        auto const bar_width =
            widths.repository > kBottomLinePrefix.size()
                ? widths.repository - kBottomLinePrefix.size()
                : 0;
        return Line(
            fmt::format("{}{}",
                        style.Green(std::string{kBottomLinePrefix}),
                        style.Bar(static_cast<double>(done) /
                                      static_cast<double>(total),
                                  static_cast<unsigned int>(bar_width))),
            DurationString(progress_->GetDuration(), style),
            SummaryString(done, total, widths.operation));
    }

    /// \brief The statistics about the repositories, of which the total
    /// amount to fetch and then the amount fetched are dropped if they do not
    /// fit.
    [[nodiscard]] auto SummaryString(std::size_t done,
                                     std::size_t total,
                                     std::size_t max_width) const
        -> std::string {
        auto summary = fmt::format("{}/{} done", done, total);
        auto const fetched = progress_->GetFetched();
        if (fetched == 0) {
            return summary;
        }
        // the goal is only known for the transfers that announce their size;
        // it is reported as the lower bound it is, and only for as long as it
        // tells more than the amount fetched so far
        auto const goal = progress_->GetGoal();
        auto with_goal = fmt::format("{}, fetched {}/{}+",
                                     summary,
                                     FormatBytes(fetched),
                                     FormatBytes(goal));
        if (goal > fetched and with_goal.size() <= max_width) {
            return with_goal;
        }
        auto with_fetched =
            fmt::format("{}, fetched {}", summary, FormatBytes(fetched));
        if (with_fetched.size() <= max_width) {
            return with_fetched;
        }
        return summary;
    }
};

}  // namespace

auto SetupReporter::Reporter(gsl::not_null<JustMRProgress*> const&
                                 progress) noexcept -> progress_reporter_t {
    return BaseProgressReporter::Reporter(SetupReporterImpl{progress},
                                          kDefaultPeriod);
}
