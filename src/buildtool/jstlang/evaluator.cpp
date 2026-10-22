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

#include "src/buildtool/jstlang/evaluator.hpp"

#include <cstddef>
#include <sstream>
#include <string>

#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/builtin_functions.hpp"
#include "src/buildtool/jstlang/ast/eval_error.hpp"
#include "src/buildtool/jstlang/ast/eval_visitor.hpp"
#include "src/buildtool/jstlang/ast/scope.hpp"
#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"
#include "src/buildtool/jstlang/ast/traversal.hpp"

namespace {

/// \brief Scope of an evaluation, created from the bindings of a file.
[[nodiscard]] auto MakeScope(jstlang::Bindings::Ptr const& bindings)
    -> jstlang::ScopePtr {
    using jstlang::Scope;
    auto const& file_bindings =
        bindings ? bindings : jstlang::Bindings::Empty();
    auto scope = Scope::BindValue(nullptr, "jst", jstlang::BuiltinsObject());
    for (auto const& [name, expr] : file_bindings->Get()) {
        scope = Scope::Bind(scope, name, expr, scope);
    }
    return scope;
}

}  // namespace

namespace jstlang {

auto Bindings::Empty() -> Ptr {
    static auto const kEmpty = std::make_shared<Bindings const>(bindings_t{});
    return kEmpty;
}

auto Evaluator::EvaluateAST(ASTNodePtr const& expr) -> ASTNodePtr {
    if (options_.imports == nullptr and bindings_ and bindings_->GetImports()) {
        options_.imports = &bindings_->GetImports();
    }
    auto fresh_names = std::size_t{};
    auto const visitor =
        ASTEvalVisitor{MakeScope(bindings_), &options_, &fresh_names};
    auto result = visitor.Evaluate(expr);
    if (options_.mode == EvalOptions::Mode::Unfold) {
        // closures in the residual code depend on the scopes of this
        // evaluation, make them self-contained
        return visitor.UnfoldClosures(result);
    }
    return result;
}

void RequireValue(ASTNodePtr const& value) {
    if (auto const* residual = FindResidual(*value)) {
        if (auto msg = FailMessage(*residual)) {
            throw ASTEvalError{*msg, residual->GetLocation()};
        }
        throw ASTEvalError{
            "Expression cannot be evaluated, it depends on unknown values.",
            residual->GetLocation()};
    }
}

auto IsSelfContained(ASTNode const& node) -> bool {
    if (node.IsLiteral()) {
        return true;
    }
    if (node.Kind() == NodeKind::Closure) {
        return false;
    }
    bool self_contained{true};
    ForEachChild(node, [&self_contained](ASTNodePtr const& child) {
        self_contained = self_contained and IsSelfContained(*child);
    });
    return self_contained;
}

auto ValueToJson(ASTNode const& value) -> nlohmann::json {
    return ASTToJsonVisitor{VerbatimType::Full, /*json_only=*/true}.ToJson(
        value);
}

auto BuiltinCallToJson(BuiltinCallNode const& call) -> nlohmann::json {
    return ASTToJsonVisitor{}.ToJson(call);
}

}  // namespace jstlang
