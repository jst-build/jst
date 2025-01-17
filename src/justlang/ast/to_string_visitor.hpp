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

#ifndef JUSTLANG_AST_TO_STRING_VISITOR_HPP
#define JUSTLANG_AST_TO_STRING_VISITOR_HPP

#include <cstddef>
#include <string>

#include "justlang/ast/ast.hpp"

namespace justlang {

class ASTToStringVisitor final {
  public:
    explicit ASTToStringVisitor(std::size_t indent = 0) noexcept
        : indent_{indent} {}

    [[nodiscard]] auto Dump(ASTNode const& node) const -> std::string;

    [[nodiscard]] auto operator()(LetNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(FuncNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(BuiltinNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(CallNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(ForeignNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(VerbatimNode const* node) const
        -> std::string;
    [[nodiscard]] auto operator()(ListNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(MapNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(BoolNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(StringNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(NumberNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(NullNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(VarNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(IfNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(ForEachNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(FoldLeftNode const* node) const
        -> std::string;
    [[nodiscard]] auto operator()(UnaryOperationNode const* node) const
        -> std::string;
    [[nodiscard]] auto operator()(LookupNode const* node) const -> std::string;
    [[nodiscard]] auto operator()(BinaryOperationNode const* node) const
        -> std::string;
    [[nodiscard]] auto operator()(RefNode const* node) const -> std::string;

  private:
    std::size_t indent_;
};

}  // namespace justlang

#endif  // JUSTLANG_AST_TO_STRING_VISITOR_HPP
