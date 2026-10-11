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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/eval_opts.hpp"
#include "src/buildtool/jstlang/ast/scope.hpp"

namespace jstlang {

/// \brief Message of a residual call to fail() with a known message.
[[nodiscard]] auto FailMessage(ASTNode const& node)
    -> std::optional<std::string>;

/// \brief First node in value that is residual code (not a value), nullptr
/// if there is none. Functions are values.
[[nodiscard]] auto FindResidual(ASTNode const& node) -> ASTNode const*;

template <class T, class... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <class T>
concept IsLeafNode = IsAnyOf<T,
                             jstlang::NullNode,
                             jstlang::BoolNode,
                             jstlang::StringNode,
                             jstlang::NumberNode,
                             jstlang::BuiltinNode,
                             jstlang::ClosureNode>;

/// \brief Evaluator for jstlang ASTs, using environments (scopes of lazily
/// evaluated bindings) instead of substitution. Each node is evaluated to a
/// value (literal, closure, built-in) or, if it depends on unknown values
/// (runtime variables), to residual code. Bindings and function arguments are
/// evaluated at most once and only if used; function bodies are evaluated
/// when the function is called. Nodes are never copied: leaves (and, see
/// ReuseUnchanged(), subtrees) that do not change are returned as
/// the very same node.
/// See EvalOptions for the modes of evaluation.
class ASTEvalVisitor final {
  public:
    /// \param fresh_names  Counter for fresh names of iteration variables in
    ///                     unfold mode (shared by the evaluation).
    explicit ASTEvalVisitor(ScopePtr scope,
                            gsl::not_null<EvalOptions const*> options,
                            std::size_t* fresh_names = nullptr) noexcept
        : scope_{std::move(scope)},
          options_{options},
          fresh_names_{fresh_names} {}

    [[nodiscard]] auto Evaluate(ASTNodePtr const& node) const -> ASTNodePtr;

    /// \brief In unfold mode: replace the closures in an unfolded value by
    /// self-contained code of their functions (see UnfoldClosure()).
    [[nodiscard]] auto UnfoldClosures(ASTNodePtr const& node) const
        -> ASTNodePtr;

  private:
    ScopePtr scope_;
    gsl::not_null<EvalOptions const*> options_;
    std::size_t* fresh_names_;

    [[nodiscard]] auto WithScope(ScopePtr scope) const -> ASTEvalVisitor {
        return ASTEvalVisitor{std::move(scope), options_, fresh_names_};
    }

    [[nodiscard]] auto IsFull() const noexcept -> bool {
        return options_->mode == EvalOptions::Mode::Full;
    }

    [[nodiscard]] auto IsUnfold() const noexcept -> bool {
        return options_->mode == EvalOptions::Mode::Unfold;
    }

    /// \brief Bind an iteration variable of a residual comprehension (or a
    /// parameter of an unfolded closure, see UnfoldClosure()): in
    /// lowering mode, it becomes a runtime variable (with the same name); in
    /// unfold mode, it is renamed to a fresh name (which cannot be written in
    /// jstlang, so it is not captured by other comprehensions) and references
    /// to it stay references.
    /// \returns the scope of the body and the name of the variable in the
    /// residual comprehension.
    [[nodiscard]] auto BindIterationVar(ScopePtr scope,
                                        std::string const& name,
                                        Location const& loc) const
        -> std::pair<ScopePtr, std::string>;

    /// \brief Whether to unroll a comprehension over a literal range of the
    /// given size (always in full evaluation).
    [[nodiscard]] auto Unroll(std::size_t size) const noexcept -> bool {
        return IsFull() or size <= options_->unroll_limit;
    }

    /// \brief Whether to return an unchanged composite node itself,
    /// preserving sharing (e.g., of residual code) when unfolding or lowering.
    /// Results of full evaluation are rebuilt, as handing out the shared nodes
    /// of parsed files to many threads makes their reference counts contend.
    [[nodiscard]] auto ReuseUnchanged() const noexcept -> bool {
        return not IsFull();
    }

    /// \brief In full evaluation, ensure that a value needed for deciding
    /// the evaluation (condition, range) is known.
    void RequireKnown(ASTNodePtr const& value,
                      std::string const& what,
                      Location const& loc) const;

    /// \brief Force the lazy binding (thunk): evaluate it on first use, store
    /// and return its value. Throws on cyclic dependencies.
    [[nodiscard]] auto Force(Scope const& binding,
                             Location const& loc) const -> ASTNodePtr;

    /// \brief In unfold mode: self-contained code of a closure, i.e., its
    /// function with the body (and default values) unfolded in the closure's
    /// scope, the parameters being unknown. The parameters keep their names
    /// (for named arguments), but are bound to fresh names at the beginning
    /// of the body (and default values), which the unfolded code refers to,
    /// so they cannot be captured by other functions.
    [[nodiscard]] auto UnfoldClosure(ClosureNode const& closure) const
        -> ASTNodePtr;

    /// \brief In unfold mode: the residual call of a function that depends on
    /// unknown values (e.g., selected from a map by a runtime variable).
    [[nodiscard]] auto UnfoldedCall(CallNode const& caller,
                                    ASTNodePtr const& target) const
        -> ASTNodePtr;

    /// \brief Call a closure with the arguments of the caller.
    [[nodiscard]] auto Apply(CallNode const& caller,
                             ClosureNode const& closure) const -> ASTNodePtr;

    /// \brief Call a built-in with the arguments of the caller.
    /// \brief In unfold mode: the residual call of a built-in function if
    /// some of its arguments depend on unknown values (and none is a
    /// function), nullptr otherwise.
    [[nodiscard]] static auto UnfoldedBuiltinCall(
        std::function<ASTNodePtr()> const& builtin_node,
        Location const& loc,
        CallNode::params_t const& params) -> ASTNodePtr;

    /// \brief Short-circuit evaluation of a list (the arguments of all() or
    /// any()): its items are evaluated in order, up to the first item whose
    /// truth value is decisive (false for all(), true for any()).
    [[nodiscard]] auto EvaluateShortCircuit(ListNode const* node,
                                            ASTNodePtr const& self,
                                            bool decisive) const -> ASTNodePtr;

    [[nodiscard]] auto ApplyBuiltin(CallNode const& caller,
                                    BuiltinNode const& builtin,
                                    ASTNodePtr const& builtin_node) const
        -> ASTNodePtr;

    [[nodiscard]] auto CallBuiltinWithParams(
        std::string const& name,
        Location const& loc,
        CallNode::params_t const& params) const -> ASTNodePtr;

    template <std::convertible_to<ASTNodePtr>... TParams>
    [[nodiscard]] auto CallBuiltin(std::string const& name,
                                   Location const& loc,
                                   TParams&&... params) const -> ASTNodePtr {
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
    [[nodiscard]] auto operator()(RefNode const* node,
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
