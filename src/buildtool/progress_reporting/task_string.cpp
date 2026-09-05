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

#include "src/buildtool/progress_reporting/task_string.hpp"

#include "fmt/core.h"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/target_map/configured_target.hpp"

auto TaskLabel(gsl::not_null<Progress*> const& progress,
               std::string const& task) -> std::string {
    if (progress->TaskTracker().IsUploading(task)) {
        return "Uploading";
    }
    auto const& label_map = progress->LabelMap();
    auto label = label_map.find(task);
    if (label != label_map.end()) {
        return label->second;
    }
    return "Executing";
}

auto TaskOrigin(gsl::not_null<Progress*> const& progress,
                std::string const& task) -> std::string {
    auto const& origin_map = progress->OriginMap();
    auto origins = origin_map.find(task);
    if (origins != origin_map.end() and not origins->second.empty()) {
        auto const& origin = origins->second[0];
        return fmt::format(
            "{}#{}", origin.first.target.ToString(), origin.second);
    }
    return task;
}
