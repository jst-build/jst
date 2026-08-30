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

#ifndef INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_SETUP_STYLE_HPP
#define INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_SETUP_STYLE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "fmt/format.h"
#include "src/other_tools/just_mr/progress_reporting/progress.hpp"

/// \brief Obtain a human-readable description of what is done for a
/// repository.
[[nodiscard]] static inline auto ToString(SetupPhase phase) -> std::string {
    switch (phase) {
        case SetupPhase::kUnpacking:
            return "unpacking";
        case SetupPhase::kImporting:
            return "importing";
        case SetupPhase::kComputing:
            return "computing";
        case SetupPhase::kFetching:
            break;
    }
    return "fetching";
}

/// \brief Obtain a human-readable amount of data, e.g., "1.2 MiB". As
/// mandated by ISO 80000-13, the unit is separated from the value by a space.
[[nodiscard]] static inline auto FormatBytes(std::uint64_t bytes)
    -> std::string {
    static constexpr std::array kUnits{"B", "KiB", "MiB", "GiB", "TiB"};
    static constexpr auto kUnitFactor = 1024.0;
    auto value = static_cast<double>(bytes);
    std::size_t unit{};
    while (value >= kUnitFactor and unit + 1 < kUnits.size()) {
        value /= kUnitFactor;
        ++unit;
    }
    // amounts of full bytes are reported without a decimal place
    if (unit == 0) {
        return fmt::format("{} B", bytes);
    }
    return fmt::format("{:.1f} {}", value, kUnits.at(unit));
}

#endif  // INCLUDED_SRC_OTHER_TOOLS_JUST_MR_PROGRESS_REPORTING_SETUP_STYLE_HPP
