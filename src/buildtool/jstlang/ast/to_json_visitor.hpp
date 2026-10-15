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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_TO_JSON_VISITOR_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_TO_JSON_VISITOR_HPP

#include <cstdint>

#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"

namespace jstlang {

/// \brief Mode of serialization. Verbatim means that the AST is serialized as
/// literal JSON instead of as an expression, and is not allowed to contain
/// calls to built-in functions. This is important for certain parts of a
/// TARGETS/RULE/EXPRESSIONS file that are not evaluated. Examples are:
///  - for all targets, fields "type" and "arguments_config"
///  - for export targets, fields "fixed_config", "doc", and "config_doc"
enum class VerbatimType : std::uint8_t {
    None,  ///< Do not do any verbatim treatment.
    Flat,  ///< Treat the AST node as verbatim, but not its children.
    Full   ///< Treat the AST node and all its children as verbatim.
};

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

    [[nodiscard]] auto operator()(BuiltinCallNode const* node) const
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

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_TO_JSON_VISITOR_HPP
