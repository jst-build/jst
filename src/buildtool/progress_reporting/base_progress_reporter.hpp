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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_PROGRESS_REPORTER_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_PROGRESS_REPORTER_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>

// Type of a progress reporter. The reporter may only block in such a way that
// it return on a notification of the condition variable; moreover, it has to
// exit once the boolean is true.
using progress_reporter_t =
    std::function<void(std::atomic<bool>*, std::condition_variable*)>;

/// \brief The base progress reporter class provides the central progress
/// reporter loop with an externally-controlled exit condition (atomic bool) and
/// activation (condition variable). Default behavior is periodic activation
/// with exponential back-off. At each activation a user-defined report function
/// is called.
///
/// The class provides a static function, which requires a user-defined report
/// function as input parameter and returns the periodically activated progress
/// reporter function. Calling this progress reporter function requires control
/// variables for termination and activation as input parameters.
///
/// Progress reporting requires a dedicated thread concurrently running besides
/// the actual worker threads executing the actions of a build. Typically, the
/// main thread creates and starts this thread (providing the progress reporter
/// function) as well as the control variables for termination and activation.
/// After finishing the regular computations, it terminates the progress
/// reporter thread using the control variables.
class BaseProgressReporter {
  public:
    [[nodiscard]] static auto Reporter(
        std::function<void(void)> report) noexcept -> progress_reporter_t;

  private:
    constexpr static std::int64_t kStartDelayMillis = 3000;
    // Scaling is roughly sqrt(2)
    constexpr static std::int64_t kDelayScalingFactorNumerator = 99;
    constexpr static std::int64_t kDelayScalingFactorDenominator = 70;
};

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_BASE_PROGRESS_REPORTER_HPP
