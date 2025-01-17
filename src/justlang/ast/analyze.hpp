// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef JUSTLANG_AST_ANALYZE_HPP
#define JUSTLANG_AST_ANALYZE_HPP

#include <stdexcept>

#include "justlang/ast/ast.hpp"

namespace justlang {

// NOLINTNEXTLINE
void StaticAnalysis(ASTNodePtr const& expr) {
    auto next = expr;
    while (auto const* let = ASTNode::Cast<LetNode const*>(next.get())) {
        next = let->GetNext();
    }

    auto const* target_map = ASTNode::Cast<MapNode const*>(next.get());
    if (target_map == nullptr) {
        throw std::runtime_error(
            "Expected top-level entity to be a literal map.");
    }

    for (auto const& [name, _] : target_map->GetFields()) {
        if (ASTNode::Cast<StringNode const*>(name.get()) == nullptr) {
            throw std::runtime_error(
                "Expected all target names to be literal strings.");
        }
    }
}

}  // namespace justlang

#endif
