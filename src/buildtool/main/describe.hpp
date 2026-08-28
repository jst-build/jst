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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP
#define INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP

#include <cstddef>
#include <optional>

#include "gsl/gsl"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/execution_api/common/api_bundle.hpp"
#include "src/buildtool/serve_api/remote/serve_api.hpp"

// Width assumed for the description if the output is not a terminal.
inline constexpr auto kDefaultDescribeWidth = 80U;

/// \brief Options for describing rules and targets.
struct DescribeOptions {
    // Print the description as JSON instead of pretty-printing it. Takes
    // precedence over brief.
    bool print_json{};
    // Omit any documentation and print names only.
    bool brief{};
    // Use ANSI escape sequences to highlight the description.
    bool colored{};
    // Width available for the description, e.g., for filling in names.
    unsigned int width{kDefaultDescribeWidth};
};

[[nodiscard]] auto DescribeTarget(
    BuildMaps::Target::ConfiguredTarget const& id,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::optional<ServeApi> const& serve,
    ApiBundle const& apis,
    std::size_t jobs,
    DescribeOptions const& options) -> int;

[[nodiscard]] auto DescribeUserDefinedRule(
    BuildMaps::Base::EntityName const& rule_name,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs,
    DescribeOptions const& options) -> int;

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_DESCRIBE_HPP
