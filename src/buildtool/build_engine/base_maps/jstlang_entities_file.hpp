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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_JSTLANG_ENTITIES_FILE_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_JSTLANG_ENTITIES_FILE_HPP

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/evaluator.hpp"

namespace BuildMaps::Base {

/// \brief File of entities (e.g., TARGETS or RULES) in jstlang for runtime
/// evaluation: the file's bindings (its top-level locals, partially evaluated
/// when loading) and the ASTs of the entities. Immutable and shared.
class JstlangEntitiesFile final {
  public:
    using Ptr = std::shared_ptr<JstlangEntitiesFile const>;
    using Logger = JstlangEvaluator::Logger;

    JstlangEntitiesFile(jstlang::Bindings::Ptr bindings,
                        std::map<std::string, jstlang::ASTNodePtr> entities)
        : bindings_{std::move(bindings)}, entities_{std::move(entities)} {
        names_.reserve(entities_.size());
        for (auto const& [name, _] : entities_) {
            names_.emplace_back(name);
        }
    }

    /// \brief Create from the (partially evaluated) AST of a file of
    /// entities (e.g., TARGETS or RULES).
    /// \param ast      The file's AST, without the built-ins binding.
    /// \param logger   Reports errors.
    /// \param imports  Resolver of the imported files, for resolving deferred
    ///                 imports; empty if the imports are resolved in the AST.
    /// \returns nullptr on error (reported via logger).
    [[nodiscard]] static auto Create(
        jstlang::ASTNodePtr const& ast,
        Logger const& logger,
        jstlang::ImportResolver imports = nullptr) noexcept -> Ptr;

    /// \brief The names of the entities in lexicographic order.
    [[nodiscard]] auto Names() const noexcept
        -> std::vector<std::string> const& {
        return names_;
    }

    [[nodiscard]] auto Contains(std::string const& name) const noexcept
        -> bool {
        return entities_.contains(name);
    }

    /// \brief Obtain the (lazily evaluated) fields of an entity.
    /// \returns nullopt on error (reported via logger).
    [[nodiscard]] auto GetFields(std::string const& name, Logger const& logger)
        const noexcept -> std::optional<JstlangEvaluator::Fields>;

  private:
    jstlang::Bindings::Ptr bindings_;
    std::map<std::string, jstlang::ASTNodePtr> entities_;
    std::vector<std::string> names_;
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_JSTLANG_ENTITIES_FILE_HPP
