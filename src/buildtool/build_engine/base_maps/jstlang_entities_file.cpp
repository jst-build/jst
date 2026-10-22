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

#include "src/buildtool/build_engine/base_maps/jstlang_entities_file.hpp"

#include <exception>

auto BuildMaps::Base::JstlangEntitiesFile::Create(
    jstlang::ASTNodePtr const& ast,
    Logger const& logger,
    jstlang::ImportResolver imports,
    bool allow_unfold) noexcept -> Ptr {
    try {
        auto node = ast;

        // top-level locals form the file's bindings
        auto bindings = jstlang::Bindings::bindings_t{};
        while (
            auto const* let =
                jstlang::ASTNode::Cast<jstlang::LetNode const*>(node.get())) {
            bindings.emplace_back(let->GetName(), let->GetValue());
            node = let->GetNext();
        }

        auto const* map =
            jstlang::ASTNode::Cast<jstlang::MapNode const*>(node.get());
        if (map == nullptr) {
            logger("Expected top-level entity to be a literal map.");
            return nullptr;
        }
        auto entities = std::map<std::string, jstlang::ASTNodePtr>{};
        for (auto const& [key, value] : map->GetFields()) {
            auto const* name =
                jstlang::ASTNode::Cast<jstlang::StringNode const*>(key.get());
            if (name == nullptr) {
                logger("Expected all names of entities to be literal strings.");
                return nullptr;
            }
            entities.insert_or_assign(name->GetValue(), value);
        }
        return std::make_shared<JstlangEntitiesFile const>(
            std::make_shared<jstlang::Bindings const>(std::move(bindings),
                                                      std::move(imports)),
            std::move(entities),
            allow_unfold);
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return nullptr;
}

auto BuildMaps::Base::JstlangEntitiesFile::GetFields(std::string const& name,
                                                     Logger const& logger)
    const noexcept -> std::optional<JstlangEvaluator::Fields> {
    auto it = entities_.find(name);
    if (it == entities_.end()) {
        logger("'" + name + "' is not defined.");
        return std::nullopt;
    }
    return JstlangEvaluator::EvaluateFields(
        it->second, bindings_, allow_unfold_, logger);
}
