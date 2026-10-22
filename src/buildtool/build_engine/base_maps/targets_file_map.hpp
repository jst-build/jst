// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TARGETS_FILE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TARGETS_FILE_MAP_HPP

#include <cstddef>
#include <exception>
#include <string>
#include <utility>

#include "fmt/format.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entities_file.hpp"
#include "src/buildtool/build_engine/base_maps/entity_definition.hpp"
#include "src/buildtool/build_engine/base_maps/json_file_map.hpp"
#include "src/buildtool/build_engine/base_maps/jstlang_entities_file.hpp"
#include "src/buildtool/build_engine/base_maps/jstlang_file_map.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_type.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

namespace BuildMaps::Base {

using TargetsFileMap = AsyncMapConsumer<ModuleName, EntitiesFile>;

/// \brief Load a jstlang entities file via the jstlang file map.
static inline void LoadJstlangEntitiesFile(
    gsl::not_null<TaskSystem*> const& ts,
    gsl::not_null<JstlangFileMap*> const& jstlang_file_map,
    jstlang::FileLocation const& file,
    AsyncMapConsumer<ModuleName, EntitiesFile>::SetterPtr const& setter,
    AsyncMapConsumerLoggerPtr const& logger) {
    jstlang_file_map->ConsumeAfterKeysReady(
        ts,
        {file},
        [file, setter, logger](auto const& values) {
            auto const& loaded = *values[0];
            auto targets_file = JstlangEntitiesFile::Create(
                loaded.ast,
                [&logger, &file](std::string const& msg) {
                    (*logger)(fmt::format("Loading file {} failed:\n{}",
                                          file.ToString(),
                                          msg),
                              /*fatal=*/true);
                },
                [imports =
                     loaded.imports](jstlang::FileLocation const& location)
                    -> jstlang::ASTNodePtr {
                    auto it = imports->find(location);
                    return it != imports->end() ? it->second : nullptr;
                });
            if (targets_file) {
                (*setter)(EntitiesFile{std::move(targets_file)});
            }
        },
        [logger](auto const& msg, bool fatal) { (*logger)(msg, fatal); });
}

/// \brief Create map for reading entities files (e.g., targets or rules
/// files), either containing JSON or jstlang.
template <RootGetter kGetRoot, FileNameGetter kGetName, JustFileType kFileType>
[[nodiscard]] auto CreateEntitiesFileMap(
    gsl::not_null<JstlangFileMap*> const& jstlang_file_map,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs) -> AsyncMapConsumer<ModuleName, EntitiesFile> {
    auto file_reader = [jstlang_file_map, repo_config](auto ts,
                                                       auto setter,
                                                       auto logger,
                                                       auto /* unused */,
                                                       auto const& key) {
        auto file = ReadModuleFile<kGetRoot, kGetName, /*kMandatory=*/true>(
            repo_config, key, logger);
        if (not file) {
            return;
        }
        auto json = nlohmann::json();
        try {
            json = nlohmann::json::parse(*file->content);
        } catch (std::exception const& e) {
            // failed, so try jstlang
            auto location =
                jstlang::FileLocation{.repo = key.repository,
                                      .path = file->path,
                                      .root = kFileType == JustFileType::kRules
                                                  ? jstlang::RootKind::Rules
                                                  : jstlang::RootKind::Targets};
            LoadJstlangEntitiesFile(
                ts, jstlang_file_map, location, setter, logger);
            return;
        }
        if (not json.is_object()) {
            (*logger)(fmt::format("JSON in {} is not an object.",
                                  file->path.string()),
                      /*fatal=*/true);
            return;
        }
        (*setter)(EntitiesFile{std::move(json)});
    };
    return AsyncMapConsumer<ModuleName, EntitiesFile>{file_reader, jobs};
}

[[nodiscard]] static inline auto CreateTargetsFileMap(
    gsl::not_null<JstlangFileMap*> const& jstlang_file_map,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs) -> TargetsFileMap {
    return CreateEntitiesFileMap<&RepositoryConfig::TargetRoot,
                                 &RepositoryConfig::TargetFileName,
                                 JustFileType::kTargets>(
        jstlang_file_map, repo_config, jobs);
}
}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_TARGETS_FILE_MAP_HPP
