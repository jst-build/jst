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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_SCOPE_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_SCOPE_HPP

#include <memory>
#include <string>
#include <utility>

#include "src/buildtool/jstlang/ast/ast.hpp"

namespace jstlang {

/// \brief Lexical scope: an immutable chain of bindings, one binding per
/// scope object (the root scope is nullptr). A binding is evaluated lazily
/// and at most once (call-by-need): it holds an expression and the scope to
/// evaluate it in, until the evaluator sets its value. Scopes are owned by a
/// single evaluation, so the cached values are not synchronized.
class Scope final {
  public:
    /// \brief Binding of an expression, evaluated in expr_scope on demand.
    explicit Scope(ScopePtr parent,
                   std::string name,
                   ASTNodePtr expr,
                   ScopePtr expr_scope) noexcept
        : parent_{std::move(parent)},
          name_{std::move(name)},
          expr_{std::move(expr)},
          expr_scope_{std::move(expr_scope)} {}

    /// \brief Binding of an already evaluated value.
    explicit Scope(ScopePtr parent, std::string name, ASTNodePtr value) noexcept
        : parent_{std::move(parent)},
          name_{std::move(name)},
          value_{std::move(value)} {}

    [[nodiscard]] static auto Bind(ScopePtr parent,
                                   std::string name,
                                   ASTNodePtr expr,
                                   ScopePtr expr_scope) -> ScopePtr {
        return std::make_shared<Scope const>(std::move(parent),
                                             std::move(name),
                                             std::move(expr),
                                             std::move(expr_scope));
    }

    [[nodiscard]] static auto BindValue(ScopePtr parent,
                                        std::string name,
                                        ASTNodePtr value) -> ScopePtr {
        return std::make_shared<Scope const>(
            std::move(parent), std::move(name), std::move(value));
    }

    /// \brief Find the innermost binding of name, nullptr if not bound.
    [[nodiscard]] static auto Find(ScopePtr const& scope,
                                   std::string const& name) noexcept
        -> Scope const* {
        for (auto const* current = scope.get(); current != nullptr;
             current = current->parent_.get()) {
            if (current->name_ == name) {
                return current;
            }
        }
        return nullptr;
    }

    [[nodiscard]] auto Name() const noexcept -> std::string const& {
        return name_;
    }

    [[nodiscard]] auto IsEvaluated() const noexcept -> bool {
        return value_ != nullptr;
    }

    [[nodiscard]] auto Value() const noexcept -> ASTNodePtr const& {
        return value_;
    }

    [[nodiscard]] auto Expr() const noexcept -> ASTNodePtr const& {
        return expr_;
    }

    [[nodiscard]] auto ExprScope() const noexcept -> ScopePtr const& {
        return expr_scope_;
    }

    [[nodiscard]] auto IsEvaluating() const noexcept -> bool {
        return evaluating_;
    }

    void SetEvaluating(bool evaluating) const noexcept {
        evaluating_ = evaluating;
    }

    /// \brief Set the value of the binding, releasing its expression.
    void SetValue(ASTNodePtr value) const noexcept {
        value_ = std::move(value);
        expr_.reset();
        expr_scope_.reset();
        evaluating_ = false;
    }

  private:
    ScopePtr parent_;
    std::string name_;
    mutable ASTNodePtr value_;
    mutable ASTNodePtr expr_;
    mutable ScopePtr expr_scope_;
    mutable bool evaluating_{};
};

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_SCOPE_HPP
