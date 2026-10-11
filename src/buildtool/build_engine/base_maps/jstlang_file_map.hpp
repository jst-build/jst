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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_JSTLANG_FILE_MAP_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_JSTLANG_FILE_MAP_HPP

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "gsl/gsl"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

/// \brief Loaded jstlang file (e.g., a targets, rules, or imported file): its
/// AST (with deferred imports) and the ASTs of all files it imports, directly
/// or transitively.
///
/// IMPORTANT: The AST is owned by the context of the file (see ASTContext),
/// and the ASTs of the imported files by the contexts of their files, i.e.,
/// by the entries of this map. Hence, the map must outlive all uses of the
/// ASTs, including their evaluation (e.g., of the definitions of targets or
/// rules read from them): do not clear the map before the evaluation of all
/// ASTs obtained from it (and anything derived from them) is finished.
struct JstlangFile {
    using imports_t =
        std::unordered_map<jstlang::FileLocation, jstlang::ASTNodePtr>;

    std::shared_ptr<jstlang::ASTContext const> context;
    jstlang::ASTNodePtr ast;
    std::shared_ptr<imports_t const> imports;
};

/// \brief Map for loading jstlang files: each file is read and parsed (with
/// deferred imports), and the files it imports are loaded transitively. Files
/// are identified by their locations, so each file is loaded only once,
/// whether it is requested as definitions file or imported. Import cycles are
/// detected as cycles of this map.
using JstlangFileMap = AsyncMapConsumer<jstlang::FileLocation, JstlangFile>;

[[nodiscard]] auto CreateJstlangFileMap(
    gsl::not_null<RepositoryConfig const*> const& repo_config,
    std::size_t jobs = 0) -> JstlangFileMap;

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_JSTLANG_FILE_MAP_HPP
