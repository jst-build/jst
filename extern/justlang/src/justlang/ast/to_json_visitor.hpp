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

#ifndef JUSTLANG_AST_TO_JSON_VISITOR_HPP
#define JUSTLANG_AST_TO_JSON_VISITOR_HPP

#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/ast.hpp"

namespace justlang {

class ASTToJsonVisitor final {
  public:
    explicit ASTToJsonVisitor(VerbatimType vtype = VerbatimType::None,
                              bool json_only = false) noexcept
        : vtype_{vtype}, json_only_{json_only} {}

    [[nodiscard]] auto ToJson(
        ASTNode const& node,
        VerbatimType next_vtype = VerbatimType::None) const -> nlohmann::json;

    // default operator for unsupported node types
    template <class T>
    [[nodiscard]] auto operator()(T const* /*node*/) const -> nlohmann::json {
        throw std::runtime_error{"Node type cannot be serialized."};
    }

    [[nodiscard]] auto operator()(VerbatimNode const* node) const
        -> nlohmann::json;
    [[nodiscard]] auto operator()(ListNode const* node) const -> nlohmann::json;
    [[nodiscard]] auto operator()(MapNode const* node) const -> nlohmann::json;
    [[nodiscard]] auto operator()(BoolNode const* node) const -> nlohmann::json;
    [[nodiscard]] auto operator()(StringNode const* node) const
        -> nlohmann::json;
    [[nodiscard]] auto operator()(NumberNode const* node) const
        -> nlohmann::json;
    [[nodiscard]] auto operator()(NullNode const* node) const -> nlohmann::json;
    [[nodiscard]] auto operator()(VarNode const* node) const -> nlohmann::json;
    [[nodiscard]] auto operator()(IfNode const* node) const -> nlohmann::json;
    [[nodiscard]] auto operator()(ForEachNode const* node) const
        -> nlohmann::json;
    [[nodiscard]] auto operator()(ZipWithNode const* node) const
        -> nlohmann::json;
    [[nodiscard]] auto operator()(FoldLeftNode const* node) const
        -> nlohmann::json;
    [[nodiscard]] auto operator()(RefNode const* node) const -> nlohmann::json;

  private:
    VerbatimType vtype_;
    bool json_only_;
};

}  // namespace justlang

#endif  // JUSTLANG_AST_TO_JSON_VISITOR_HPP
