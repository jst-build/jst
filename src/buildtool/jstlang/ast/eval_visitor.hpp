// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_VISITOR_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_VISITOR_HPP

#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/scope.hpp"

namespace jstlang {

class ASTEvalError : public std::runtime_error {
  public:
    explicit ASTEvalError(std::string const& error);
    explicit ASTEvalError(std::string const& error,
                          jstlang::Location const& loc);
};

template <class T, class... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <class T>
concept IsLeafNode = IsAnyOf<T,
                             jstlang::NullNode,
                             jstlang::BoolNode,
                             jstlang::StringNode,
                             jstlang::NumberNode,
                             jstlang::RefNode,
                             jstlang::BuiltinNode,
                             jstlang::ClosureNode>;

/// \brief Evaluator for jstlang ASTs, using environments (scopes of lazily
/// evaluated bindings) instead of substitution. Each node is evaluated to a
/// value (literal, closure, built-in) or, if it depends on unknown values
/// (runtime variables), to residual code. Bindings and function arguments are
/// evaluated at most once and only if used; function bodies are evaluated
/// when the function is called. Nodes are never copied: leaves and subtrees
/// that do not change are returned as the very same node.
/// The residual code is suitable for lowering to the expression language:
/// iteration variables of loops over unknown ranges are runtime variables.
class ASTEvalVisitor final {
  public:
    explicit ASTEvalVisitor(ScopePtr scope) noexcept
        : scope_{std::move(scope)} {}

    [[nodiscard]] auto Evaluate(ASTNodePtr const& node) const -> ASTNodePtr;

  private:
    ScopePtr scope_;

    [[nodiscard]] static auto WithScope(ScopePtr scope) -> ASTEvalVisitor {
        return ASTEvalVisitor{std::move(scope)};
    }

    /// \brief Force the lazy binding (thunk): evaluate it on first use, store
    /// and return its value. Throws on cyclic dependencies.
    [[nodiscard]] static auto Force(Scope const& binding,
                                    Location const& loc) -> ASTNodePtr;

    /// \brief Call a closure with the arguments of the caller.
    [[nodiscard]] auto Apply(CallNode const& caller,
                             ClosureNode const& closure) const -> ASTNodePtr;

    /// \brief Call a built-in with the arguments of the caller.
    [[nodiscard]] auto ApplyBuiltin(CallNode const& caller,
                                    BuiltinNode const& builtin) const
        -> ASTNodePtr;

    [[nodiscard]] static auto CallBuiltinWithParams(
        std::string const& name,
        Location const& loc,
        CallNode::params_t const& params) -> ASTNodePtr;

    template <std::convertible_to<ASTNodePtr>... TParams>
    [[nodiscard]] static auto CallBuiltin(std::string const& name,
                                          Location const& loc,
                                          TParams&&... params) -> ASTNodePtr {
        auto call_params = CallNode::params_t{
            std::make_pair(std::nullopt, std::forward<TParams>(params))...};
        return CallBuiltinWithParams(name, loc, call_params);
    }

  public:
    // default operator for leaf nodes (values, evaluate to themselves)
    template <class T>
    [[nodiscard]] auto operator()(T const* /*node*/,
                                  ASTNodePtr const& self) const -> ASTNodePtr {
        // use static_assert to check concept with meaningful error message
        static_assert(IsLeafNode<T>,
                      "non-leaf nodes require a specialized operator");
        return self;
    }

    [[nodiscard]] auto operator()(LetNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(FuncNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(CallNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ForeignNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(BuiltinCallNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ListNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(MapNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(VarNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(IfNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ForEachNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ZipWithNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(FoldLeftNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(UnaryOperationNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(LookupNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(BinaryOperationNode const* node,
                                  ASTNodePtr const& self) const -> ASTNodePtr;
};

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_VISITOR_HPP
