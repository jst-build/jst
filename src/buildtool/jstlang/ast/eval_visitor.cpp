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

#include "src/buildtool/jstlang/ast/eval_visitor.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/builtin_functions.hpp"
#include "src/buildtool/jstlang/ast/eval_error.hpp"
#include "src/buildtool/jstlang/ast/scope.hpp"
#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"
#include "src/buildtool/jstlang/ast/traversal.hpp"

namespace {

auto const kEmptyList =
    std::make_shared<jstlang::ListNode>(jstlang::Location{},
                                        jstlang::ListNode::items_t{});

auto const kMinusOne =
    std::make_shared<jstlang::NumberNode>(jstlang::Location{}, -1.0);

template <std::same_as<jstlang::ValueType>... TTypes>
[[nodiscard]] auto CheckUnaryType(jstlang::ASTNodePtr const& expr,
                                  jstlang::ValueType type,
                                  TTypes... types) -> bool {
    if constexpr (sizeof...(TTypes) > 0) {
        return (expr->EvalType() == type) or CheckUnaryType(expr, types...);
    }
    return expr->EvalType() == type;
}

template <std::same_as<jstlang::ValueType>... TTypes>
[[nodiscard]] auto CheckBinaryTypes(jstlang::ASTNodePtr const& lhs,
                                    jstlang::ASTNodePtr const& rhs,
                                    TTypes... types) -> bool {
    return CheckUnaryType(lhs, types...) and CheckUnaryType(rhs, types...);
}

}  // namespace

namespace jstlang {

auto ASTEvalVisitor::CallBuiltinWithParams(
    std::string const& name,
    Location const& loc,
    CallNode::params_t const& params) const -> ASTNodePtr {
    auto bt_it = kBuiltinFunctions.find(name);
    if (bt_it == kBuiltinFunctions.end()) {
        throw ASTEvalError{"Unknown builtin function \"" + name + "\".", loc};
    }
    if (options_->mode == EvalOptions::Mode::Unfold) {
        // e.g., built-in functions implementing operators
        if (auto residual = UnfoldedBuiltinCall(
                [&name]() -> ASTNodePtr {
                    return std::make_shared<BuiltinNode>(name);
                },
                loc,
                params)) {
            return residual;
        }
    }
    auto eval_callback = [this](ASTNodePtr const& node,
                                ScopePtr const& scope) -> ASTNodePtr {
        return WithScope(scope).Evaluate(node);
    };
    try {
        return std::invoke(bt_it->second, loc, eval_callback, params);
    } catch (std::exception const& e) {
        throw ASTEvalError{e.what()};
    }
}

auto FailMessage(ASTNode const& node) -> std::optional<std::string> {
    if (auto const* call = ASTNode::Cast<BuiltinCallNode const*>(&node)) {
        if (call->GetName() == "fail" and not call->GetArgs().empty() and
            call->GetArgs()[0] != nullptr) {
            auto const& msg = call->GetArgs()[0];
            if (auto const* str = ASTNode::Cast<StringNode const*>(msg.get())) {
                return str->GetValue();
            }
            if (msg->IsLiteral()) {
                return ASTToJsonVisitor{VerbatimType::Full, /*json_only=*/true}
                    .ToJson(*msg)
                    .dump();
            }
        }
    }
    return std::nullopt;
}

auto FindResidual(ASTNode const& node) -> ASTNode const* {
    if (node.IsLiteral()) {
        return nullptr;  // no need to traverse literals
    }
    switch (node.Kind()) {
        case NodeKind::Null:
        case NodeKind::Bool:
        case NodeKind::Number:
        case NodeKind::String:
        case NodeKind::Ref:
        case NodeKind::Builtin:
        case NodeKind::Closure:
            return nullptr;
        case NodeKind::List:
            for (auto const& item :
                 ASTNode::Cast<ListNode const*>(&node)->GetItems()) {
                if (auto const* residual = FindResidual(*item)) {
                    return residual;
                }
            }
            return nullptr;
        case NodeKind::Map:
            for (auto const& [key, value] :
                 ASTNode::Cast<MapNode const*>(&node)->GetFields()) {
                if (auto const* residual = FindResidual(*key)) {
                    return residual;
                }
                if (auto const* residual = FindResidual(*value)) {
                    return residual;
                }
            }
            return nullptr;
        default:
            return &node;
    }
}

auto ASTEvalVisitor::UnfoldedBuiltinCall(
    std::function<ASTNodePtr()> const& builtin_node,
    Location const& loc,
    CallNode::params_t const& params) -> ASTNodePtr {
    // calls with arguments depending on unknown values stay calls (evaluated
    // later), unless functions are passed (e.g., to foreach(), which unfolds to
    // a comprehension of the function's body)
    bool all_known{true};
    std::size_t count{};  // without missing trailing arguments
    for (std::size_t i{}; i < params.size(); ++i) {
        auto const& value = params[i].second;
        if (not value) {
            continue;
        }
        if (value->Kind() == NodeKind::Closure or
            value->Kind() == NodeKind::Builtin) {
            return nullptr;
        }
        if (i != count) {
            return nullptr;  // missing argument in between
        }
        all_known = all_known and FindResidual(*value) == nullptr;
        count = i + 1;
    }
    if (all_known) {
        return nullptr;
    }
    return std::make_shared<CallNode>(
        loc,
        CallNode::target_t{builtin_node()},
        CallNode::params_t{
            params.begin(),
            params.begin() + static_cast<std::ptrdiff_t>(count)});
}

void ASTEvalVisitor::RequireKnown(ASTNodePtr const& value,
                                  std::string const& what,
                                  Location const& loc) const {
    if (not IsFull()) {
        return;
    }
    if (auto msg = FailMessage(*value)) {
        throw ASTEvalError{*msg, loc};
    }
    throw ASTEvalError{
        "Cannot evaluate " + what + ", it depends on unknown values.", loc};
}

auto ASTEvalVisitor::Evaluate(ASTNodePtr const& node) const -> ASTNodePtr {
    if (node) {
        return std::visit(
            [this, &node](auto const* typed_node) -> ASTNodePtr {
                return (*this)(typed_node, node);
            },
            node->ToVariant());
    }
    return nullptr;
}

auto ASTEvalVisitor::Force(Scope const& binding,
                           Location const& loc) const -> ASTNodePtr {
    if (not binding.IsEvaluated()) {
        if (binding.IsEvaluating()) {
            throw ASTEvalError{
                "Cyclic dependency on variable '" + binding.Name() + "'.", loc};
        }
        binding.SetEvaluating(true);
        // the value is set (and the expression released) after evaluation
        auto const& expr = binding.Expr();
        try {
            binding.SetValue(WithScope(binding.ExprScope()).Evaluate(expr));
        } catch (...) {
            binding.SetEvaluating(false);
            throw;
        }
    }
    return binding.Value();
}

auto ASTEvalVisitor::operator()(LetNode const* node, ASTNodePtr const& /*self*/)
    const -> ASTNodePtr {
    // bind the value lazily, evaluated in the current scope when used
    auto next_scope =
        Scope::Bind(scope_, node->GetName(), node->GetValue(), scope_);
    return WithScope(std::move(next_scope)).Evaluate(node->GetNext());
}

auto ASTEvalVisitor::operator()(FuncNode const* node,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    // functions evaluate to closures, their body is evaluated when called
    return std::make_shared<ClosureNode>(
        node->GetLocation(), ASTNode::CastPtr<FuncNode>(self), scope_);
}

auto ASTEvalVisitor::operator()(CallNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto const& target = node->GetTarget();

    if (std::holds_alternative<CallNode::expr_t>(target)) {
        auto value = Evaluate(std::get<CallNode::expr_t>(target));
        if (auto const* builtin =
                ASTNode::Cast<BuiltinNode const*>(value.get())) {
            return ApplyBuiltin(*node, *builtin, value);
        }
        if (auto const* closure =
                ASTNode::Cast<ClosureNode const*>(value.get())) {
            return Apply(*node, *closure);
        }
        if (IsUnfold() and FindResidual(*value) != nullptr) {
            return UnfoldedCall(*node, value);
        }
        throw ASTEvalError{"Call target is not a function.",
                           node->GetLocation()};
    }

    if (std::holds_alternative<CallNode::builtin_t>(target)) {
        auto const& builtin = std::get<CallNode::builtin_t>(target);
        return ApplyBuiltin(*node, *builtin, builtin);
    }

    if (std::holds_alternative<CallNode::identifier_t>(target)) {
        auto const& name = std::get<CallNode::identifier_t>(target);
        auto const* binding = Scope::Find(scope_, name);
        if (binding == nullptr) {
            throw ASTEvalError{
                "Cannot find function or variable '" + name + "' in scope.",
                node->GetLocation()};
        }

        auto value = Force(*binding, node->GetLocation());
        if (node->IsVarRead()) {
            // variable is read, return its value (functions are closures)
            return value;
        }

        // variable is called like a function, must contain a function
        if (auto const* closure =
                ASTNode::Cast<ClosureNode const*>(value.get())) {
            return Apply(*node, *closure);
        }
        if (auto const* builtin =
                ASTNode::Cast<BuiltinNode const*>(value.get())) {
            return ApplyBuiltin(*node, *builtin, value);
        }
        if (IsUnfold() and FindResidual(*value) != nullptr) {
            return UnfoldedCall(*node, value);
        }
        throw ASTEvalError{
            "Cannot call a variable that does not contain a function.",
            node->GetLocation()};
    }

    throw ASTEvalError{"Unsupported call target.", node->GetLocation()};
}

auto ASTEvalVisitor::operator()(ForeignNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto expr = node->GetExpr();
    if (not expr and options_->imports != nullptr) {
        expr = (*options_->imports)(*node->GetImport());
    }
    if (not expr) {
        throw ASTEvalError{
            "Unresolved import of file " + node->GetImport()->ToString() + ".",
            node->GetLocation()};
    }
    // foreign AST is self-contained, evaluate in a clean scope binding only
    // the built-ins object (as value, per thread), with the same options
    thread_local auto const kBuiltinsScope =
        Scope::BindValue(nullptr, "jst", BuiltinsObject());
    return WithScope(kBuiltinsScope).Evaluate(expr);
}

auto ASTEvalVisitor::operator()(BuiltinCallNode const* node,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    // Built-in calls are retained for later serialization, only their
    // arguments are evaluated again.
    bool changed{};
    auto args = BuiltinCallNode::args_t{};
    args.reserve(node->GetArgs().size());
    for (auto const& arg : node->GetArgs()) {
        args.emplace_back(arg ? Evaluate(arg) : nullptr);
        changed = changed or args.back() != arg;
    }
    if (not changed and ReuseUnchanged()) {
        return self;
    }
    return std::make_shared<BuiltinCallNode>(node->GetLocation(),
                                             node->GetName(),
                                             std::move(args),
                                             node->EvalType());
}

auto ASTEvalVisitor::operator()(ListNode const* node,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    bool changed{};
    auto items = ListNode::items_t{};
    items.reserve(node->GetItems().size());
    for (auto const& item : node->GetItems()) {
        items.emplace_back(Evaluate(item));
        changed = changed or items.back() != item;
    }
    if (not changed and ReuseUnchanged()) {
        return self;
    }
    return std::make_shared<ListNode>(node->GetLocation(), std::move(items));
}

auto ASTEvalVisitor::operator()(MapNode const* node,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    auto const& fields = node->GetFields();
    auto values = MapNode::fields_t{};
    values.reserve(fields.size());
    bool changed{};
    bool has_new_literal_names{};
    for (auto const& field : fields) {
        auto name = Evaluate(field.first);
        // field names must evaluate to strings
        if (name->EvalType() != ValueType::String and
            name->EvalType() != ValueType::Any) {
            throw ASTEvalError{
                "Expected string expression for field name, but got: " +
                    TypeToString(name->EvalType()),
                field.first->GetLocation()};
        }
        if (ASTNode::Cast<StringNode const*>(field.first.get()) == nullptr and
            ASTNode::Cast<StringNode const*>(name.get()) != nullptr) {
            // computed name became a string literal
            has_new_literal_names = true;
        }
        auto value = Evaluate(field.second);
        changed = changed or name != field.first or value != field.second;
        values.emplace_back(std::move(name), std::move(value));
    }
    if (not changed and ReuseUnchanged()) {
        return self;
    }
    return std::make_shared<MapNode>(
        node->GetLocation(),
        std::move(values),
        has_new_literal_names ? MapNode::Unique::Maybe : MapNode::Unique::Yes);
}

auto ASTEvalVisitor::operator()(VarNode const* node,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    if (not IsFull()) {
        // runtime variable, value unknown
        return self;
    }
    if (options_->config == nullptr) {
        throw ASTEvalError{"Runtime variable '" + node->GetName() +
                               "' cannot be read in static evaluation.",
                           node->GetLocation()};
    }
    // like the expression language, a variable set to null is not set
    if (auto value = (*options_->config)(node->GetName());
        value != nullptr and value->Kind() != NodeKind::Null) {
        return value;
    }
    if (node->GetDefault()) {
        return Evaluate(node->GetDefault());
    }
    return std::make_shared<NullNode>(node->GetLocation());
}

auto ASTEvalVisitor::operator()(RefNode const* node,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    if (not IsFull()) {
        return self;
    }
    // in full evaluation, a reference is its value in the expression language
    // (a string or a list of strings), as for values of unknown type, the
    // expression language does not distinguish references from other values
    auto json =
        ASTToJsonVisitor{VerbatimType::Full, /*json_only=*/true}.ToJson(*node);
    auto const& loc = node->GetLocation();
    if (json.is_string()) {
        return std::make_shared<StringNode>(loc, json.get<std::string>());
    }
    auto items = ListNode::items_t{};
    items.reserve(json.size());
    for (auto const& item : json) {
        items.emplace_back(
            std::make_shared<StringNode>(loc, item.get<std::string>()));
    }
    return std::make_shared<ListNode>(loc, std::move(items));
}

auto ASTEvalVisitor::operator()(IfNode const* node, ASTNodePtr const& /*self*/)
    const -> ASTNodePtr {
    auto cond = Evaluate(node->GetCondition());

    // attempt partial evaluation
    if (auto cond_value = cond->TruthValue()) {
        auto branch = *cond_value ? Evaluate(node->GetThenBranch())
                                  : Evaluate(node->GetElseBranch());
        return branch ? std::move(branch) : kEmptyList;
    }
    RequireKnown(cond, "condition", node->GetLocation());

    return std::make_shared<IfNode>(node->GetLocation(),
                                    std::move(cond),
                                    Evaluate(node->GetThenBranch()),
                                    Evaluate(node->GetElseBranch()));
}

auto ASTEvalVisitor::operator()(ForEachNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto const& loc = node->GetLocation();
    auto const& var_name = node->GetVariable();
    auto range = Evaluate(node->GetRange());

    // must be list or runtime variable
    if (range->EvalType() != ValueType::List and
        range->EvalType() != ValueType::Any) {
        throw ASTEvalError{
            "Expected list expression for foreach range argument, but got: " +
                TypeToString(range->EvalType()),
            loc};
    }

    auto const* list = ASTNode::Cast<ListNode const*>(range.get());
    if (list != nullptr and Unroll(list->GetItems().size())) {
        // literal array -> do unrolling
        auto items = ListNode::items_t{};
        items.reserve(list->GetItems().size());
        for (auto const& item : list->GetItems()) {
            auto body_scope = Scope::BindValue(scope_, var_name, item);
            items.emplace_back(
                WithScope(std::move(body_scope)).Evaluate(node->GetBody()));
        }
        return std::make_shared<ListNode>(loc, std::move(items));
    }

    RequireKnown(range, "range", loc);

    // Evaluate the body with the iteration variable unknown, evaluated during
    // runtime.
    auto [body_scope, residual_var] = BindIterationVar(scope_, var_name, loc);
    return std::make_shared<ForEachNode>(
        loc,
        std::move(residual_var),
        std::move(range),
        WithScope(std::move(body_scope)).Evaluate(node->GetBody()));
}

auto ASTEvalVisitor::operator()(ZipWithNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto const& var1_name = node->GetVariable1();
    auto const& var2_name = node->GetVariable2();
    auto range1 = Evaluate(node->GetRange1());
    auto range2 = Evaluate(node->GetRange2());

    auto const* list1 = ASTNode::Cast<ListNode const*>(range1.get());
    auto const* list2 = ASTNode::Cast<ListNode const*>(range2.get());
    if (list1 != nullptr and list2 != nullptr and
        Unroll(std::min(list1->GetItems().size(), list2->GetItems().size()))) {
        // literal arrays -> do unrolling
        auto const count =
            std::min(list1->GetItems().size(), list2->GetItems().size());
        auto items = ListNode::items_t{};
        items.reserve(count);
        for (std::size_t i{}; i < count; ++i) {
            auto body_scope =
                Scope::BindValue(scope_, var1_name, list1->GetItems()[i]);
            body_scope = Scope::BindValue(
                std::move(body_scope), var2_name, list2->GetItems()[i]);
            items.emplace_back(
                WithScope(std::move(body_scope)).Evaluate(node->GetBody()));
        }
        return std::make_shared<ListNode>(node->GetLocation(),
                                          std::move(items));
    }
    if (list1 != nullptr and list1->GetItems().empty()) {
        return std::make_shared<ListNode>(node->GetLocation(),
                                          ListNode::items_t{});
    }
    if (list2 != nullptr and list2->GetItems().empty()) {
        return std::make_shared<ListNode>(node->GetLocation(),
                                          ListNode::items_t{});
    }

    // Evaluate the body with the iteration variables unknown, evaluated during
    // runtime.
    auto const& loc = node->GetLocation();
    RequireKnown(list1 == nullptr ? range1 : range2, "range", loc);
    auto [scope1, residual_var1] = BindIterationVar(scope_, var1_name, loc);
    auto [body_scope, residual_var2] =
        BindIterationVar(std::move(scope1), var2_name, loc);
    return std::make_shared<ZipWithNode>(
        loc,
        std::move(residual_var1),
        std::move(residual_var2),
        std::move(range1),
        std::move(range2),
        WithScope(std::move(body_scope)).Evaluate(node->GetBody()));
}

auto ASTEvalVisitor::operator()(FoldLeftNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto const& iter_var_name = node->GetIterVar();
    auto const& accu_var_name = node->GetAccuVar();
    auto range = Evaluate(node->GetRange());
    auto init = Evaluate(node->GetInit());
    auto const& body = node->GetBody();

    if (auto const* list = ASTNode::Cast<ListNode const*>(range.get());
        list != nullptr and Unroll(list->GetItems().size())) {
        // literal array -> do unroll fold
        auto accu = init;
        for (auto const& item : list->GetItems()) {
            auto body_scope = Scope::BindValue(scope_, accu_var_name, accu);
            body_scope =
                Scope::BindValue(std::move(body_scope), iter_var_name, item);
            accu = WithScope(std::move(body_scope)).Evaluate(body);
        }
        return accu;
    }

    // Evaluate the body with the iteration and accumulation variables
    // unknown, evaluated during runtime.
    auto const& loc = node->GetLocation();
    RequireKnown(range, "range", loc);
    auto [iter_scope, residual_iter_var] =
        BindIterationVar(scope_, iter_var_name, loc);
    auto [body_scope, residual_accu_var] =
        BindIterationVar(std::move(iter_scope), accu_var_name, loc);
    return std::make_shared<FoldLeftNode>(
        loc,
        std::move(residual_iter_var),
        std::move(residual_accu_var),
        std::move(init),
        std::move(range),
        WithScope(std::move(body_scope)).Evaluate(body));
}

auto ASTEvalVisitor::operator()(UnaryOperationNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto expr = Evaluate(node->GetExpression());

    // resolve UnaryOperationNode
    auto loc = node->GetLocation();
    if (IsUnfold() and FindResidual(*expr) != nullptr) {
        // stays an operation (its type is known when evaluated)
        return std::make_shared<UnaryOperationNode>(
            loc, node->GetType(), std::move(expr));
    }
    switch (node->GetType()) {
        case UnaryOperationNode::Type::Minus:
            if (CheckUnaryType(expr, ValueType::Any, ValueType::Number)) {
                auto args = std::make_shared<ListNode>(
                    loc, ListNode::items_t{std::move(expr), kMinusOne});
                return CallBuiltin("prod", loc, std::move(args));
            }
            throw ASTEvalError{"Unsupported type for unary minus operation: " +
                                   TypeToString(expr->EvalType()),
                               loc};
        case UnaryOperationNode::Type::Not:
            return CallBuiltin("not", loc, std::move(expr));
    }

    // unreachable
    throw ASTEvalError{"Resolving unary operation failed.", loc};
}

auto ASTEvalVisitor::operator()(LookupNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto container = Evaluate(node->GetContainer());
    auto index = Evaluate(node->GetIndex());
    auto default_value = Evaluate(node->GetDefault());
    auto const& loc = node->GetLocation();
    if (IsUnfold() and (FindResidual(*container) != nullptr or
                        FindResidual(*index) != nullptr)) {
        // stays a lookup (the type of the container is known when evaluated)
        return std::make_shared<LookupNode>(loc,
                                            std::move(index),
                                            std::move(container),
                                            std::move(default_value));
    }

    if (ASTNode::Cast<MapNode const*>(container.get()) != nullptr) {
        auto result = CallBuiltin("get", loc, index, container, default_value);
        // if the result is the default value, it's an error
        if (result == default_value) {
            auto field_name = std::string{};
            if (auto const* lookup_name =
                    ASTNode::Cast<StringNode const*>(index.get())) {
                field_name = " '" + lookup_name->GetValue() + "'";
            }
            throw ASTEvalError{"Cannot find field name" + field_name + ".",
                               loc};
        }
        return result;
    }

    if (ASTNode::Cast<ListNode const*>(container.get()) != nullptr) {
        auto result = CallBuiltin("at", loc, index, container, default_value);
        // if the result is the default value, it's an error
        if (result == default_value) {
            throw ASTEvalError{"Invalid index.", loc};
        }
        return result;
    }

    throw ASTEvalError{
        "Failed to resolve the type of the container for LookupNode. Please "
        "use built-in functions.",
        loc};
}

auto ASTEvalVisitor::operator()(BinaryOperationNode const* node,
                                ASTNodePtr const& /*self*/) const
    -> ASTNodePtr {
    auto const& loc = node->GetLocation();
    auto lhs = Evaluate(node->GetLhs());

    // short-circuit evaluation: the right operand is not evaluated if the
    // left one already decides the result of && and ||
    if (node->GetType() == BinaryOperationNode::Type::And or
        node->GetType() == BinaryOperationNode::Type::Or) {
        bool const decisive = node->GetType() == BinaryOperationNode::Type::Or;
        if (auto truth = lhs->TruthValue(); truth and *truth == decisive) {
            return std::make_shared<BoolNode>(loc, decisive);
        }
    }
    auto rhs = Evaluate(node->GetRhs());

    // resolve BinaryOperationNode
    if (IsUnfold() and
        (FindResidual(*lhs) != nullptr or FindResidual(*rhs) != nullptr)) {
        // stays an operation (the types of its operands are known when
        // evaluated)
        return std::make_shared<BinaryOperationNode>(
            loc, node->GetType(), std::move(lhs), std::move(rhs));
    }
    ASTNodePtr args = node->GetType() == BinaryOperationNode::Type::Equal
                          ? nullptr
                          : std::make_shared<ListNode>(
                                loc, jstlang::ListNode::items_t{lhs, rhs});
    switch (node->GetType()) {
        case BinaryOperationNode::Type::Plus:
            if (lhs->EvalType() == rhs->EvalType() and
                lhs->EvalType() == ValueType::Any) {
                throw ASTEvalError{
                    "Cannot deduce argument types, please use explicit "
                    "join/sum/flatten/union built-in function.",
                    loc};
            }
            if (CheckBinaryTypes(lhs, rhs, ValueType::Any, ValueType::String)) {
                return CallBuiltin("join", loc, std::move(args));
            }
            if (CheckBinaryTypes(lhs, rhs, ValueType::Any, ValueType::Number)) {
                return CallBuiltin("sum", loc, std::move(args));
            }
            if (CheckBinaryTypes(lhs, rhs, ValueType::Any, ValueType::List)) {
                return CallBuiltin("flatten", loc, std::move(args));
            }
            if (CheckBinaryTypes(lhs, rhs, ValueType::Any, ValueType::Map)) {
                return CallBuiltin("union", loc, std::move(args));
            }
            break;
        case BinaryOperationNode::Type::Multiplication:
            if (CheckBinaryTypes(lhs, rhs, ValueType::Any, ValueType::Number)) {
                return CallBuiltin("prod", loc, std::move(args));
            }
            break;
        case BinaryOperationNode::Type::And:
            return CallBuiltin("all", loc, std::move(args));
        case BinaryOperationNode::Type::Or:
            return CallBuiltin("any", loc, std::move(args));
        case BinaryOperationNode::Type::Equal:
            return CallBuiltin("eq", loc, lhs, rhs);
    }

    throw ASTEvalError{"Unsupported types for binary operation. Found " +
                           TypeToString(lhs->EvalType()) + " and " +
                           TypeToString(rhs->EvalType()) + ".",
                       loc};
}

auto ASTEvalVisitor::UnfoldClosures(ASTNodePtr const& node) const
    -> ASTNodePtr {
    if (node->IsLiteral()) {
        return node;
    }
    if (auto const* closure = ASTNode::Cast<ClosureNode const*>(node.get())) {
        return UnfoldClosure(*closure);
    }
    return MapChildren(node, [this](ASTNodePtr const& child) {
        return UnfoldClosures(child);
    });
}

auto ASTEvalVisitor::UnfoldClosure(ClosureNode const& closure) const
    -> ASTNodePtr {
    auto const& func = closure.GetFunc();
    auto const& loc = func.GetLocation();
    auto scope = closure.GetScope();
    // fresh names of the preceding parameters and their original names
    auto renamed = std::vector<std::pair<std::string, std::string>>{};
    auto bind_renamed = [&renamed, &loc](ASTNodePtr expr) {
        for (auto const& [fresh, name] : std::views::reverse(renamed)) {
            expr = std::make_shared<LetNode>(
                loc,
                fresh,
                std::make_shared<CallNode>(loc, CallNode::target_t{name}),
                std::move(expr));
        }
        return expr;
    };
    auto params = FuncNode::params_t{};
    params.reserve(func.GetParams().size());
    for (auto const& [name, default_expr] : func.GetParams()) {
        auto unfolded_default = ASTNodePtr{};
        if (default_expr) {
            unfolded_default = bind_renamed(
                UnfoldClosures(WithScope(scope).Evaluate(default_expr)));
        }
        params.emplace_back(name, std::move(unfolded_default));
        auto [param_scope, fresh] =
            BindIterationVar(std::move(scope), name, loc);
        scope = std::move(param_scope);
        renamed.emplace_back(std::move(fresh), name);
    }
    auto body = bind_renamed(
        UnfoldClosures(WithScope(std::move(scope)).Evaluate(func.GetBody())));
    return std::make_shared<FuncNode>(loc, std::move(params), std::move(body));
}

auto ASTEvalVisitor::UnfoldedCall(CallNode const& caller,
                                  ASTNodePtr const& target) const
    -> ASTNodePtr {
    // the function is only known when evaluated, so the call stays a call
    // (with the arguments unfolded)
    auto params = CallNode::params_t{};
    if (auto const& caller_params = caller.GetParams()) {
        params.reserve(caller_params->size());
        for (auto const& [name, expr] : *caller_params) {
            params.emplace_back(name, Evaluate(expr));
        }
    }
    return std::make_shared<CallNode>(
        caller.GetLocation(), CallNode::target_t{target}, std::move(params));
}

auto ASTEvalVisitor::Apply(CallNode const& caller,
                           ClosureNode const& closure) const -> ASTNodePtr {
    if (caller.IsVarRead()) {
        // expected function call, but this is a variable read
        throw ASTEvalError{"Expected function call.", caller.GetLocation()};
    }

    auto const& callee = closure.GetFunc();
    auto const& callee_params = callee.GetParams();
    if (callee_params.empty()) {
        // function without arguments, just evaluate body
        return WithScope(closure.GetScope()).Evaluate(callee.GetBody());
    }

    auto const& params = caller.GetParams();
    if (not params) {
        throw ASTEvalError{"Missing parameter list for calling function.",
                           caller.GetLocation()};
    }

    // Assign caller arguments to callee parameters: unnamed arguments are
    // assigned by position, named arguments are assigned by name.
    auto args = std::vector<ASTNodePtr const*>(callee_params.size(), nullptr);
    std::size_t pos{};
    for (auto const& [cname, cexpr] : *params) {
        auto idx = pos;
        if (cname) {
            // potentially quadratic complexity here; only matters for many
            // named arguments, but in reality the list is small and therefore
            // the linear search is faster than building an index map.
            auto param_it = std::find_if(
                callee_params.begin(),
                callee_params.end(),
                [&cname](auto const& param) { return param.first == *cname; });
            if (param_it == callee_params.end()) {
                throw ASTEvalError{
                    "Unknown function argument '" + *cname + "'.",
                    caller.GetLocation()};
            }
            idx = static_cast<std::size_t>(
                std::distance(callee_params.begin(), param_it));
        }
        else if (pos++ >= callee_params.size()) {
            throw ASTEvalError{"Too many arguments for calling function.",
                               caller.GetLocation()};
        }
        if (args[idx] != nullptr) {
            throw ASTEvalError{"Function argument '" +
                                   callee_params[idx].first +
                                   "' specified more than once.",
                               caller.GetLocation()};
        }
        args[idx] = &cexpr;
    }

    // Bind arguments lazily: values specified by the caller are evaluated in
    // the caller's scope, default values in the function's scope (with the
    // preceding parameters).
    try {
        auto func_scope = closure.GetScope();
        for (std::size_t i{}; i < callee_params.size(); ++i) {
            auto const& [pname, pdefault] = callee_params[i];
            if (args[i] != nullptr) {
                func_scope = Scope::Bind(func_scope, pname, *args[i], scope_);
            }
            else if (pdefault) {
                func_scope =
                    Scope::Bind(func_scope, pname, pdefault, func_scope);
            }
            else {
                throw ASTEvalError{"Missing function argument '" + pname + "'.",
                                   callee.GetLocation()};
            }
        }
        return WithScope(std::move(func_scope)).Evaluate(callee.GetBody());
    } catch (std::exception const& e) {
        throw ASTEvalError{"Evaluating function call failed with:",
                           caller.GetLocation(),
                           e.what()};
    }
}

auto ASTEvalVisitor::BindIterationVar(ScopePtr scope,
                                      std::string const& name,
                                      Location const& loc) const
    -> std::pair<ScopePtr, std::string> {
    if (IsUnfold() and fresh_names_ != nullptr) {
        auto fresh = name + "#" + std::to_string((*fresh_names_)++);
        auto reference =
            std::make_shared<CallNode>(loc, CallNode::target_t{fresh});
        return {Scope::BindValue(std::move(scope), name, std::move(reference)),
                std::move(fresh)};
    }
    return {Scope::BindValue(
                std::move(scope), name, std::make_shared<VarNode>(loc, name)),
            name};
}

auto ASTEvalVisitor::EvaluateShortCircuit(ListNode const* node,
                                          ASTNodePtr const& self,
                                          bool decisive) const -> ASTNodePtr {
    bool changed{};
    auto items = ListNode::items_t{};
    items.reserve(node->GetItems().size());
    for (auto const& item : node->GetItems()) {
        items.emplace_back(Evaluate(item));
        changed = changed or items.back() != item;
        if (auto truth = items.back()->TruthValue();
            truth and *truth == decisive) {
            break;
        }
    }
    if (not changed and items.size() == node->GetItems().size() and
        ReuseUnchanged()) {
        return self;
    }
    return std::make_shared<ListNode>(node->GetLocation(), std::move(items));
}

auto ASTEvalVisitor::ApplyBuiltin(CallNode const& caller,
                                  BuiltinNode const& builtin,
                                  ASTNodePtr const& builtin_node) const
    -> ASTNodePtr {
    if (caller.IsVarRead()) {
        // function without arguments, just return body
        throw ASTEvalError{"Expected function call.", caller.GetLocation()};
    }

    // Evaluate call parameters
    auto const& caller_params = caller.GetParams();
    if (not caller_params) {
        throw ASTEvalError{"Missing parameter list for function call.",
                           caller.GetLocation()};
    }
    auto const& bt_name = builtin.GetName();
    // literal list argument of all() and any() is evaluated with short-circuit
    bool const is_short_circuit = bt_name == "all" or bt_name == "any";
    auto params = CallNode::params_t{};
    params.reserve(caller_params->size());
    for (auto const& [pname, pexpr] : *caller_params) {
        auto const* list = ASTNode::Cast<ListNode const*>(pexpr.get());
        if (is_short_circuit and list != nullptr) {
            params.emplace_back(
                pname,
                EvaluateShortCircuit(
                    list, pexpr, /*decisive=*/bt_name == "any"));
        }
        else {
            params.emplace_back(pname, Evaluate(pexpr));
        }
    }

    auto const& loc = caller.GetLocation();
    if (IsUnfold()) {
        if (auto residual = UnfoldedBuiltinCall(
                [&builtin_node]() { return builtin_node; }, loc, params)) {
            return residual;
        }
    }

    // return builtin function result
    auto result = CallBuiltinWithParams(bt_name, loc, params);

    if (bt_name == "env" and IsFull()) {
        // read the runtime variable from the configuration
        return Evaluate(result);
    }
    if (bt_name == "env" and options_->mode == EvalOptions::Mode::Lowering) {
        // Built-in function env() emits a runtime variable reference (VarNode).
        // Those are typically conflict free, except for rare cases. One such
        // case is the iteration variable of a runtime-evaluated expression
        // (foreach,foldl), which is added to the scope as a runtime variable.
        // (see ASTEvalVisitor::operator()(ForEachNode const*))
        // Example:
        //     "[ foo + jst.env('foo') for foo in jst.env('range') ]"
        // Due to range not being statically known, the iteration variable foo
        // will become a runtime variable, potentially shadowing the environment
        // variable referred to via jst.env('foo'). The following code checks
        // for this scenario and throws an error to avoid unexpected behavior.
        auto const* var = ASTNode::Cast<VarNode const*>(result.get());
        if (var == nullptr) {
            throw ASTEvalError{
                "Expected var node from env() built-in function.", loc};
        }
        auto const& var_name = var->GetName();
        auto const* binding = Scope::Find(scope_, var_name);
        if (binding != nullptr and binding->IsEvaluated() and
            ASTNode::Cast<VarNode const*>(binding->Value().get()) != nullptr) {
            throw ASTEvalError{"Config variable '" + var_name +
                                   "' is shadowed by scope variable.",
                               loc};
        }
    }

    return result;
}

}  // namespace jstlang
