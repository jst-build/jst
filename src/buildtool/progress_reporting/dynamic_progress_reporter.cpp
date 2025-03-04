// Copyright 2024 Huawei Cloud Computing Technology Co., Ltd.
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
#include <climits>
#include <optional>
#include <string>
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h>

#include "fmt/color.h"
#include "fmt/core.h"

namespace {

class DynamicProgressReporterImpl {
  private:
    static auto constexpr kMaxTasks = 8;
    static auto constexpr kMaxCount = 10;
    static auto constexpr kDefaultMaxWidth = 80U;
    static auto constexpr kTaskLabelFrac = 38.0 / (kDefaultMaxWidth - 3);
    static auto constexpr kDescriptionFrac = 12.0 / (kDefaultMaxWidth - 2);
    static auto constexpr kProgressBarFrac = 22.0 / (kDefaultMaxWidth - 2);
    static auto constexpr kColorGreen = fg(fmt::color::lime_green);
    static auto constexpr kColorBlue = fg(fmt::color::light_blue);
    static const std::string kThreeDots;

    struct State {
        int cached;
        int run;
        int queued;
        std::vector<std::string> samples;
        auto operator==(State const& other) const -> bool {
            return cached == other.cached and run == other.run and
                   queued == other.queued and samples == other.samples;
        }
    };

  public:
    explicit DynamicProgressReporterImpl(
        gsl::not_null<Statistics*> const& stats,
        gsl::not_null<Progress*> const& progress,
        Logger const* logger)
        : stats_{stats}, progress_{progress}, logger_{logger} {}

    auto operator()() -> void {
        // Note: order matters; queued has to be queried last
        State state = {.cached = stats_->ActionsCachedCounter(),
                       .run = stats_->ActionsExecutedCounter(),
                       .queued = stats_->ActionsQueuedCounter(),
                       .samples = progress_->TaskTracker().Sample(kMaxTasks)};

        if (state_ == state and count_++ < kMaxCount) {
            // only update on change, but honor max count to force redraw
            return;
        }
        count_ = 0;
        state_ = state;

        // determine progress parameters
        auto total = gsl::narrow<int>(progress_->OriginMap().size());
        auto num_samples = gsl::narrow<int>(state_.samples.size());
        auto done = state_.run + state_.cached;
        auto active = state_.queued - state_.run - state_.cached;

        // determine terminal width
        auto width = TerminalWidth();
        if (not width) {
            width = UINT_MAX;
        }

        // determine maximum label string width
        auto max_label_width = 0U;
        for (auto const& sample : state_.samples) {
            auto label_width =
                static_cast<unsigned int>(LabelString(sample).size());
            max_label_width = std::max(max_label_width, label_width);
        }

        // print a line for each currently running task
        std::string progress_message{};
        for (auto const& sample : state_.samples) {
            progress_message +=
                TaskString(sample, *width, max_label_width) + "\n";
        }
        if (num_samples > 0 and active > num_samples) {
            progress_message += TaskContinuationString(*width) + "\n";
        }

        // print bottom line
        progress_message += BottomLineString(done, total, active, *width);

        Logger::LogVolatile(logger_, LogLevel::Progress, progress_message);
    }

  private:
    gsl::not_null<Statistics*> stats_;
    gsl::not_null<Progress*> progress_;
    Logger const* logger_;
    int count_{};
    State state_{};

    [[nodiscard]] auto OriginString(std::string const& sample,
                                    unsigned int max_width) -> std::string {
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
        if (result.size() > max_width) {
            std::string mark{};
            switch (result[0]) {
                case '[':
                    mark = "[";
                    break;
                case '\'':
                    mark = "'";
                    break;
                default:
                    break;
            }
            result = mark + kThreeDots +
                     result.substr(result.size() - max_width + mark.size() +
                                   kThreeDots.size());
        }
        return result;
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

    [[nodiscard]] auto LabelString(std::string const& sample,
                                   unsigned int max_width) -> std::string {
        return LabelString(sample).substr(0, max_width);
    }

    [[nodiscard]] auto TaskString(std::string const& sample,
                                  unsigned int max_width,
                                  unsigned int max_label_width) -> std::string {
        auto label_width = static_cast<unsigned int>(
            (std::min(max_width, kDefaultMaxWidth) - 3) * kTaskLabelFrac);
        label_width = std::min(label_width, max_label_width);
        auto origin_width = max_width - label_width - 3;
        auto label_str = LabelString(sample, label_width);
        return fmt::format(
            "  {} {}",
            Blue(fmt::format(
                "{:.<{}}",
                label_str + (label_str.size() < label_width ? " " : ""),
                label_width)),
            OriginString(sample, origin_width));
    }

    [[nodiscard]] static auto TaskContinuationString(unsigned int max_width)
        -> std::string {
        auto desc_width = static_cast<unsigned int>(
            (std::min(max_width, kDefaultMaxWidth) - 2) * kDescriptionFrac);
        return fmt::format(
            "{:>{}}", (kThreeDots + " ").substr(0, desc_width), desc_width);
    }

    [[nodiscard]] auto SummaryString(int done,
                                     int total,
                                     int active,
                                     unsigned int max_width) -> std::string {
        return fmt::format("{}/{} done, {} cached, {} processing.",
                           done,
                           total,
                           state_.cached,
                           active)
            .substr(0, max_width);
    }

    [[nodiscard]] auto BottomLineString(int done,
                                        int total,
                                        int active,
                                        unsigned int max_width) -> std::string {
        auto desc_width = static_cast<unsigned int>(
            (std::min(max_width, kDefaultMaxWidth) - 2) * kDescriptionFrac);
        auto bar_width = static_cast<unsigned int>(
            (std::min(max_width, kDefaultMaxWidth) - 2) * kProgressBarFrac);
        auto summary_width = max_width - desc_width - bar_width - 2;
        return fmt::format(
            "{} {} {}",
            Green(fmt::format("{:>{}}",
                              std::string{"Building"}.substr(0, desc_width),
                              desc_width)),
            ProgressBar(done, total, bar_width),
            SummaryString(done, total, active, summary_width));
    }

    [[nodiscard]] static auto ProgressBar(int done,
                                          int total,
                                          unsigned int max_width,
                                          char body = '=',
                                          char tip = '>',
                                          char empty = ' ') -> std::string {
        std::string result{};
        auto done_bar_width = static_cast<unsigned int>(
            static_cast<double>(done) / total * (max_width - 2));
        result += "[";
        if (done_bar_width > 1) {
            result += std::string(done_bar_width - 1, body);
        }
        if (done_bar_width > 0) {
            result += std::string{tip};
        }
        result += std::string((max_width - 2) - done_bar_width, empty);
        result += "]";
        return result;
    }

    [[nodiscard]] static auto TerminalWidth() -> std::optional<unsigned int> {
        struct winsize ws {};
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg, hicpp-vararg)
        if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == -1) {
            return std::nullopt;
        }
        return ws.ws_col;
    }

    [[nodiscard]] static auto Green(std::string msg) -> std::string {
        return fmt::format(kColorGreen, "{}", msg);
    }

    [[nodiscard]] static auto Blue(std::string msg) -> std::string {
        return fmt::format(kColorBlue, "{}", msg);
    }
};

const std::string DynamicProgressReporterImpl::kThreeDots{"..."};

}  // namespace

auto DynamicProgressReporter::Reporter(gsl::not_null<Statistics*> const& stats,
                                       gsl::not_null<Progress*> const& progress,
                                       Logger const* logger) noexcept
    -> progress_reporter_t {
    return BaseProgressReporter::Reporter(
        DynamicProgressReporterImpl{stats, progress, logger},
        kDefaultPeriod,
        kDefaultBackoffFactor);
}
