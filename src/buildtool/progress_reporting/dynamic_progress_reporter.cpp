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

#include <cmath>
#include <string>
#include <vector>

#include "fmt/color.h"
#include "fmt/core.h"

namespace {

class DynamicProgressReporterImpl {
  private:
    static auto constexpr kMaxTasks = 8;
    static auto constexpr kMaxCount = 10;
    static auto constexpr kProgressBarWidth = 20;
    static auto constexpr kColorGreen = fg(fmt::color::lime_green);
    static auto constexpr kColorBlue = fg(fmt::color::light_blue);

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
        auto done = state_.run + state_.cached;
        auto active = static_cast<std::size_t>(state_.queued - state_.run -
                                               state_.cached);

        // print a line for each currently running task
        std::string progress_message{};
        if (active > 0 and not state_.samples.empty()) {
            auto size = state_.samples.size();
            for (std::size_t i{0}; i < size; ++i) {
                auto const& sample = state_.samples[i];
                progress_message += fmt::format(
                    "{} {}\n",
                    Blue(fmt::format("{:>12}", GetLabelString(sample))),
                    GetOriginString(sample));
            }
            if (active > size) {
                progress_message +=
                    fmt::format("{}\n", fmt::format("{:>12}", "... "));
            }
        }

        // print bottom line
        progress_message +=
            fmt::format("{} {} {}.",
                        Green(fmt::format("{:>12}", "Building")),
                        ProgressBar(done, total),
                        fmt::format("{}/{} done, {} cached, {} processing",
                                    done,
                                    total,
                                    state_.cached,
                                    active));

        Logger::LogVolatile(logger_, LogLevel::Progress, progress_message);
    }

  private:
    gsl::not_null<Statistics*> stats_;
    gsl::not_null<Progress*> progress_;
    Logger const* logger_;
    int count_{};
    State state_{};

    [[nodiscard]] auto GetOriginString(std::string const& sample)
        -> std::string {
        auto const& origin_map = progress_->OriginMap();
        auto origins = origin_map.find(sample);
        if (origins != origin_map.end() and not origins->second.empty()) {
            auto const& origin = origins->second[0];
            return fmt::format(
                "{}#{}", origin.first.target.ToString(), origin.second);
        }
        return sample;
    }

    [[nodiscard]] auto GetLabelString(std::string const& sample)
        -> std::string {
        std::string label_string{};
        if (progress_->TaskTracker().IsUploading(sample)) {
            label_string = "Uploading";
        }
        else {
            label_string = "Executing";
        }
        return label_string;
    }

    [[nodiscard]] static auto ProgressBar(int done,
                                          int total,
                                          char body = '=',
                                          char tip = '>',
                                          char empty = ' ') -> std::string {
        std::string progress_bar{};
        auto done_bar_width = static_cast<std::size_t>(
            std::round(static_cast<double>(done) / total * kProgressBarWidth));
        progress_bar += "[";
        if (done_bar_width > 1) {
            progress_bar += std::string(done_bar_width - 1, body);
        }
        if (done_bar_width > 0) {
            progress_bar += std::string(1, tip);
        }
        progress_bar += std::string(kProgressBarWidth - done_bar_width, empty);
        progress_bar += "]";
        return progress_bar;
    }

    [[nodiscard]] static auto Green(std::string msg) -> std::string {
        return fmt::format(kColorGreen, "{}", msg);
    }

    [[nodiscard]] static auto Blue(std::string msg) -> std::string {
        return fmt::format(kColorBlue, "{}", msg);
    }
};

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
