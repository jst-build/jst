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

#include "justlang/ast/inline_visitor.hpp"

#include <cmath>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "justlang/ast/ast.hpp"
#include "justlang/ast/builtin_functions.hpp"
#include "justlang/ast/inline.hpp"

namespace {

auto const kEmptyList =
    std::make_shared<justlang::ListNode>(justlang::Location{},
                                         justlang::ListNode::items_t{});

auto const kMinusOne =
    std::make_shared<justlang::NumberNode>(justlang::Location{}, -1.0);

[[nodiscard]] auto CallBuiltinWithParams(
    std::string const& name,
    justlang::Location const& loc,
    justlang::CallNode::params_t const& params) -> justlang::BuiltIn::Result {
    auto bt_it = justlang::kBuiltinFunctions.find(name);
    if (bt_it == justlang::kBuiltinFunctions.end()) {
        throw justlang::ASTInlineError{
            "Unknown builtin function \"" + name + "\".", loc};
    }
    try {
        return std::invoke(bt_it->second, loc, params);
    } catch (std::exception const& e) {
        throw justlang::ASTInlineError{e.what()};
    }
}

template <std::convertible_to<justlang::ASTNodePtr>... TParams>
[[nodiscard]] auto CallBuiltin(std::string const& name,
                               justlang::Location const& loc,
                               TParams&&... params)
    -> justlang::BuiltIn::Result {
    auto call_params = justlang::CallNode::params_t{
        std::make_pair(std::nullopt, std::forward<TParams>(params))...};
    return CallBuiltinWithParams(name, loc, call_params);
}

template <std::same_as<justlang::ValueType>... TTypes>
[[nodiscard]] auto CheckUnaryType(justlang::ASTNodePtr const& expr,
                                  justlang::ValueType type,
                                  TTypes... types) -> bool {
    if constexpr (sizeof...(TTypes) > 0) {
        return (expr->EvalType() == type) or CheckUnaryType(expr, types...);
    }
    return expr->EvalType() == type;
}

template <std::same_as<justlang::ValueType>... TTypes>
[[nodiscard]] auto CheckBinaryTypes(justlang::ASTNodePtr const& lhs,
                                    justlang::ASTNodePtr const& rhs,
                                    TTypes... types) -> bool {
    return CheckUnaryType(lhs, types...) and CheckUnaryType(rhs, types...);
}

}  // namespace

namespace justlang {

auto ASTInlineVisitor::InlineFunctions(ASTNodePtr const& node) const
    -> ASTNodePtr {
    if (node) {
        return std::visit(*this, node->ToVariant());
    }
    return nullptr;
}

auto ASTInlineVisitor::operator()(LetNode const* node) const -> ASTNodePtr {
    // name of function / variable
    auto const& name = node->GetName();
    auto const& value = node->GetValue();
    auto const& next = node->GetNext();

    auto next_scope = ScopeDefs(scope_);
    next_scope.AddDef(name, InlineFunctions(value));

    // Always fully inline LetNodes
    // TODO(oreiche): once we support RULES, this wont happen unconditionally
    return ASTInlineVisitor{&next_scope, partial_inline_}.InlineFunctions(next);
}

auto ASTInlineVisitor::operator()(FuncNode const* node) const -> ASTNodePtr {
    // For every function or variable (function without arguments):
    //   - run inlining on the function's body
    //   - add inlined body to scope
    //   - return subsequent expression, inlined with updated scope

    // function arguments
    FuncNode::params_t params{};
    // create new scope for partial inlining without built-ins, which will be
    // performed later (with more type info available) when the full inlining
    // is done at the function call.
    auto func_scope = ScopeDefs{scope_};
    // partial inlining if
    // - current scope requires it (this is a nested FuncNode)
    // - or the FuncNode has arguments
    auto partial_inline = partial_inline_ or not node->GetParams().empty();
    auto func_visitor = ASTInlineVisitor{&func_scope, partial_inline};
    for (auto const& [pname, pexpr] : node->GetParams()) {
        // inline possible var/func uses in argument expression
        auto inl_pexpr = func_visitor.InlineFunctions(pexpr);
        params.emplace_back(pname, std::move(inl_pexpr));
        // add parameter to scope as nullptr, indicating it is yet unknown
        func_scope.AddDef(pname, nullptr);
    }

    // inline possible var/func uses in func body
    auto inl_func_body = func_visitor.InlineFunctions(node->GetBody());

    return std::make_shared<FuncNode>(
        node->GetLocation(), std::move(params), std::move(inl_func_body));
}

auto ASTInlineVisitor::operator()(CallNode const* node) const -> ASTNodePtr {
    // Inline function call
    auto const& target = node->GetTarget();

    if (std::holds_alternative<CallNode::expr_t>(target)) {
        auto inl_target = InlineFunctions(std::get<CallNode::expr_t>(target));
        if (auto const* builtin =
                ASTNode::Cast<BuiltinNode const*>(inl_target.get())) {
            return InlineBuiltinCall(node, builtin);
        }
        if (auto const* func =
                ASTNode::Cast<FuncNode const*>(inl_target.get())) {
            return InlineFuncBody(node, func);
        }
        if (partial_inline_) {
            // partially inlined AST, retain this call for full inlining later
            return std::make_shared<CallNode>(
                node->GetLocation(), inl_target, node->GetParams());
        }
        throw ASTInlineError{"Call target is not a function.",
                             node->GetLocation()};
    }

    if (std::holds_alternative<CallNode::builtin_t>(target)) {
        return InlineBuiltinCall(
            node,
            ASTNode::Cast<BuiltinNode const*>(
                std::get<CallNode::builtin_t>(target).get()));
    }

    if (std::holds_alternative<CallNode::identifier_t>(target)) {
        auto const& name = std::get<CallNode::identifier_t>(target);
        auto const* entry = scope_->GetDef(name);
        if (entry == nullptr) {
            throw ASTInlineError{
                "Cannot find function or variable '" + name + "' in scope.",
                node->GetLocation()};
        }

        if (auto const* func = ASTNode::Cast<FuncNode const*>(entry->get())) {
            // target is a function
            if (node->IsVarRead()) {
                // function is read like a variable, return the entire FuncNode
                return ShallowCopy(func);
            }
            // function is called, inline its body
            return InlineFuncBody(node, func);
        }

        // target is a variable
        auto const& var_value = *entry;
        if (var_value == nullptr) {
            // variable is unknown, must be a function parameter during
            // partial inlining
            if (partial_inline_) {
                // retain call node for full inlining later
                return ShallowCopy(node);
            }
            throw ASTInlineError{"Found unknown variable during full inlining.",
                                 node->GetLocation()};
        }

        if (node->IsVarRead()) {
            // variable is read, return its AST
            return var_value;
        }

        // variable is called like a function, must contain a function
        if (auto const* func =
                ASTNode::Cast<FuncNode const*>(var_value.get())) {
            return InlineFuncBody(node, func);
        }
        throw ASTInlineError{
            "Cannot call a variable that does not contain a function.",
            node->GetLocation()};
    }

    throw ASTInlineError{"Unsupported call target.", node->GetLocation()};
}

auto ASTInlineVisitor::operator()(ForeignNode const* node) const -> ASTNodePtr {
    // foreign AST is self-contained, set up new inliner with clean scope
    auto clean_scope = ScopeDefs{};
    auto foreign_inliner =
        ASTInlineVisitor{&clean_scope, /*partial_inline=*/false};
    auto ast = ProvideBuiltinsToAST(node->GetExpr());
    return foreign_inliner.InlineFunctions(ast);
}

auto ASTInlineVisitor::operator()(VerbatimNode const* node) const
    -> ASTNodePtr {
    // Inlining verbatim nodes should never happen. Verbatim nodes are only
    // created during augmentation (after inlining) or during inlining CallNodes
    // to built-in functions, which should not be inlined a second time.
    throw ASTInlineError{"Inlining Verbatim nodes is not supported.",
                         node->GetLocation()};
}

auto ASTInlineVisitor::operator()(ListNode const* node) const -> ASTNodePtr {
    auto items = ListNode::items_t{};
    items.reserve(node->GetItems().size());
    for (auto const& item : node->GetItems()) {
        items.emplace_back(InlineFunctions(item));
    }
    return std::make_shared<ListNode>(node->GetLocation(), std::move(items));
}

auto ASTInlineVisitor::operator()(MapNode const* node) const -> ASTNodePtr {
    auto const& fields = node->GetFields();
    auto inl_fields = MapNode::fields_t{};
    inl_fields.reserve(fields.size());
    for (auto const& field : fields) {
        inl_fields.emplace_back(InlineFunctions(field.first),
                                InlineFunctions(field.second));
    }
    return std::make_shared<MapNode>(node->GetLocation(),
                                     std::move(inl_fields));
}

auto ASTInlineVisitor::operator()(IfNode const* node) const -> ASTNodePtr {
    auto inl_cond = InlineFunctions(node->GetCondition());

    // attempt partial evaluation
    if (auto cond_value = inl_cond->TruthValue()) {
        auto inl_branch = *cond_value ? InlineFunctions(node->GetThenBranch())
                                      : InlineFunctions(node->GetElseBranch());
        return inl_branch ? std::move(inl_branch) : kEmptyList;
    }

    return std::make_shared<IfNode>(node->GetLocation(),
                                    inl_cond,
                                    InlineFunctions(node->GetThenBranch()),
                                    InlineFunctions(node->GetElseBranch()));
}

auto ASTInlineVisitor::operator()(ForEachNode const* node) const -> ASTNodePtr {
    auto const& var_name = node->GetVariable();
    auto range = InlineFunctions(node->GetRange());

    if (auto const* list = ASTNode::Cast<ListNode const*>(range.get())) {
        // literal array -> do unrolling to inline all variable references
        auto inl_items = ListNode::items_t{};
        inl_items.reserve(list->GetItems().size());
        for (auto const& item : list->GetItems()) {
            auto body_scope = ScopeDefs{scope_};
            body_scope.AddDef(var_name, item);
            auto body_visitor = ASTInlineVisitor{&body_scope, partial_inline_};
            inl_items.emplace_back(
                body_visitor.InlineFunctions(node->GetBody()));
        }
        return std::make_shared<ListNode>(node->GetLocation(),
                                          std::move(inl_items));
    }

    // Add iteration variable to scope for inlining the iteration body.
    auto body_scope = ScopeDefs{scope_};
    auto const& loc = node->GetLocation();
    if (partial_inline_) {
        // For partial inline, add iteration variable to scope as nullptr,
        // indicating it is yet unknown.
        body_scope.AddDef(var_name, nullptr);
    }
    else {
        // For full inlining, replace CallNode to iteration variable by VarNode
        // (runtime variable) with the same name, evaluated during runtime
        auto runtime_read_expr = std::make_shared<VarNode>(loc, var_name);
        body_scope.AddDef(var_name, std::move(runtime_read_expr));
    }
    auto body_visitor = ASTInlineVisitor{&body_scope, partial_inline_};
    return std::make_shared<ForEachNode>(
        loc,
        var_name,
        std::move(range),
        body_visitor.InlineFunctions(node->GetBody()));
}

auto ASTInlineVisitor::operator()(FoldLeftNode const* node) const
    -> ASTNodePtr {
    auto const& iter_var_name = node->GetIterVar();
    auto const& accu_var_name = node->GetAccuVar();
    auto range = InlineFunctions(node->GetRange());
    auto init = InlineFunctions(node->GetInit());
    auto const& body = node->GetBody();

    if (auto const* list = ASTNode::Cast<ListNode const*>(range.get())) {
        // literal array -> do unroll fold to inline all variable references
        auto eval_scope = ScopeDefs(scope_);
        auto dummy_loc = Location{};
        auto accu = init;
        for (auto const& item : list->GetItems()) {
            eval_scope.AddDef(accu_var_name, accu);
            eval_scope.AddDef(iter_var_name, item);
            accu =
                ASTInlineVisitor{&eval_scope, partial_inline_}.InlineFunctions(
                    body);
        }
        return accu;
    }

    // Add iteration variable to scope for inlining the iteration body.
    auto body_scope = ScopeDefs{scope_};
    auto const& loc = node->GetLocation();
    if (partial_inline_) {
        // For partial inline, add iteration and accumulation variables to scope
        // as nullptr, indicating they are yet unknown.
        body_scope.AddDef(iter_var_name, nullptr);
        body_scope.AddDef(accu_var_name, nullptr);
    }
    else {
        // For full inlining, replace CallNode to iteration and accumulation
        // variables by VarNodes (runtime variables) with the same name,
        // evaluated during runtime
        auto iter_read_expr = std::make_shared<VarNode>(loc, iter_var_name);
        auto accu_read_expr = std::make_shared<VarNode>(loc, accu_var_name);
        body_scope.AddDef(iter_var_name, std::move(iter_read_expr));
        body_scope.AddDef(accu_var_name, std::move(accu_read_expr));
    }
    auto body_visitor = ASTInlineVisitor{&body_scope, partial_inline_};
    return std::make_shared<FoldLeftNode>(loc,
                                          iter_var_name,
                                          accu_var_name,
                                          std::move(init),
                                          std::move(range),
                                          body_visitor.InlineFunctions(body));
}

auto ASTInlineVisitor::operator()(UnaryOperationNode const* node) const
    -> ASTNodePtr {
    auto inl_expr = InlineFunctions(node->GetExpression());

    if (partial_inline_) {
        // retain UnaryOperationNode
        return std::make_shared<UnaryOperationNode>(node->GetLocation(),
                                                    node->GetType(),
                                                    std::move(inl_expr),
                                                    node->EvalType());
    }

    // resolve UnaryOperationNode
    auto loc = node->GetLocation();
    switch (node->GetType()) {
        case UnaryOperationNode::Type::Minus:
            if (CheckUnaryType(inl_expr, ValueType::Any, ValueType::Number)) {
                auto args = std::make_shared<ListNode>(
                    loc, ListNode::items_t{std::move(inl_expr), kMinusOne});
                return CallBuiltin("prod", loc, std::move(args)).GetNode();
            }
            throw ASTInlineError{
                "Unsupported type for unary minus operation: " +
                    TypeToString(inl_expr->EvalType()),
                loc};
        case UnaryOperationNode::Type::Not:
            return CallBuiltin("not", loc, std::move(inl_expr)).GetNode();
    }

    // unreachable
    throw ASTInlineError{"Resolving unary operation failed.", loc};
}

auto ASTInlineVisitor::operator()(LookupNode const* node) const -> ASTNodePtr {
    auto inl_container = InlineFunctions(node->GetContainer());
    auto inl_index = InlineFunctions(node->GetIndex());
    auto inl_default = InlineFunctions(node->GetDefault());
    auto const& loc = node->GetLocation();

    if (ASTNode::Cast<MapNode const*>(inl_container.get()) != nullptr) {
        auto result =
            CallBuiltin("get", loc, inl_index, inl_container, inl_default);
        // if the result is the default value, it's an error
        if (result.GetNode() == inl_default) {
            auto field_name = std::string{};
            if (auto const* lookup_name =
                    ASTNode::Cast<StringNode const*>(inl_index.get())) {
                field_name = " '" + lookup_name->GetValue() + "'";
            }
            throw ASTInlineError{"Cannot find field name" + field_name + ".",
                                 loc};
        }
        return result.GetNode();
    }

    if (ASTNode::Cast<ListNode const*>(inl_container.get()) != nullptr) {
        auto result =
            CallBuiltin("at", loc, inl_index, inl_container, inl_default);
        // if the result is the default value, it's an error
        if (result.GetNode() == inl_default) {
            throw ASTInlineError{"Invalid index.", loc};
        }
        return result.GetNode();
    }

    throw ASTInlineError{
        "Failed to resolve the type of the container for LookupNode. Please "
        "use built-in functions.",
        loc};
}

auto ASTInlineVisitor::operator()(BinaryOperationNode const* node) const
    -> ASTNodePtr {
    auto inl_lhs = InlineFunctions(node->GetLhs());
    auto inl_rhs = InlineFunctions(node->GetRhs());

    if (partial_inline_) {
        // retain BinaryOperationNode
        auto value_type = node->EvalType();
        return std::make_shared<BinaryOperationNode>(
            node->GetLocation(), node->GetType(), inl_lhs, inl_rhs, value_type);
    }

    // resolve BinaryOperationNode
    auto const& loc = node->GetLocation();
    ASTNodePtr args =
        node->GetType() == BinaryOperationNode::Type::Equal
            ? nullptr
            : std::make_shared<ListNode>(
                  loc, justlang::ListNode::items_t{inl_lhs, inl_rhs});
    switch (node->GetType()) {
        case BinaryOperationNode::Type::Plus:
            if (inl_lhs->EvalType() == inl_rhs->EvalType() and
                inl_lhs->EvalType() == ValueType::Any) {
                throw ASTInlineError{
                    "Cannot deduce argument types, please use explicit "
                    "join/sum/flatten/union built-in function.",
                    loc};
            }
            if (CheckBinaryTypes(
                    inl_lhs, inl_rhs, ValueType::Any, ValueType::String)) {
                return CallBuiltin("join", loc, std::move(args)).GetNode();
            }
            if (CheckBinaryTypes(
                    inl_lhs, inl_rhs, ValueType::Any, ValueType::Number)) {
                return CallBuiltin("sum", loc, std::move(args)).GetNode();
            }
            if (CheckBinaryTypes(
                    inl_lhs, inl_rhs, ValueType::Any, ValueType::List)) {
                return CallBuiltin("flatten", loc, std::move(args)).GetNode();
            }
            if (CheckBinaryTypes(
                    inl_lhs, inl_rhs, ValueType::Any, ValueType::Map)) {
                return CallBuiltin("union", loc, std::move(args)).GetNode();
            }
            break;
        case BinaryOperationNode::Type::Multiplication:
            if (CheckBinaryTypes(
                    inl_lhs, inl_rhs, ValueType::Any, ValueType::Number)) {
                return CallBuiltin("prod", loc, std::move(args)).GetNode();
            }
            break;
        case BinaryOperationNode::Type::And:
            return CallBuiltin("all", loc, std::move(args)).GetNode();
        case BinaryOperationNode::Type::Or:
            return CallBuiltin("any", loc, std::move(args)).GetNode();
        case BinaryOperationNode::Type::Equal:
            return CallBuiltin("eq", loc, inl_lhs, inl_rhs).GetNode();
    }

    throw ASTInlineError{"Unsupported types for binary operation. Found " +
                             TypeToString(inl_lhs->EvalType()) + " and " +
                             TypeToString(inl_rhs->EvalType()) + ".",
                         loc};
}

auto ASTInlineVisitor::InlineFuncBody(CallNode const* caller,
                                      FuncNode const* callee) const
    -> ASTNodePtr {
    if (caller->IsVarRead()) {
        // expected function call, but this is a variable read
        throw ASTInlineError{"Expected function call.", caller->GetLocation()};
    }

    if (callee->GetParams().empty()) {
        // function without arguments, just return body
        return callee->GetBody();
    }

    std::size_t num_mandatory{};
    for (auto const& [_, default_arg] : callee->GetParams()) {
        num_mandatory += default_arg ? 0UL : 1UL;
    }

    auto const& params = caller->GetParams();
    if (not params) {
        throw ASTInlineError{"Missing parameter list for calling function.",
                             caller->GetLocation()};
    }
    if (params->size() < num_mandatory) {
        throw ASTInlineError{
            "Missing mandatory arguments for calling function.",
            caller->GetLocation()};
    }

    // Create new scope for inlining function body and param default values
    auto func_scope = ScopeDefs{scope_};
    auto func_visitor = ASTInlineVisitor{&func_scope, partial_inline_};
    try {
        for (std::size_t i{}; i < callee->GetParams().size(); ++i) {
            auto const& [pname, pdefault] = callee->GetParams()[i];
            auto def_name = pname;
            auto def_expr = pdefault;

            if (i < params->size()) {
                // assign value specified by caller
                auto const& [cname, cexpr] = params->at(i);
                if (cname) {
                    def_name = *cname;
                }
                def_expr = InlineFunctions(cexpr);
            }
            else {
                // assign default value
                def_expr = func_visitor.InlineFunctions(pdefault);
            }

            if (not def_expr) {
                throw ASTInlineError{
                    "Missing function argument '" + def_name + "'.",
                    callee->GetLocation()};
            }

            auto loc = def_expr->GetLocation();
            func_scope.AddDef(def_name, std::move(def_expr));
        }
        return func_visitor.InlineFunctions(callee->GetBody());
    } catch (std::exception const& e) {
        throw ASTInlineError{
            std::string{"Inlining function call failed with:\n"} + e.what(),
            caller->GetLocation()};
    }
}

auto ASTInlineVisitor::InlineBuiltinCall(CallNode const* caller,
                                         BuiltinNode const* builtin) const
    -> ASTNodePtr {
    if (caller->IsVarRead()) {
        // function without arguments, just return body
        throw ASTInlineError{"Expected function call.", caller->GetLocation()};
    }

    // Inline call parameters
    auto const& caller_params = caller->GetParams();
    if (not caller_params) {
        throw ASTInlineError{"Missing parameter list for function call.",
                             caller->GetLocation()};
    }
    auto params = CallNode::params_t{};
    params.reserve(caller_params->size());
    for (auto const& [pname, pexpr] : *caller_params) {
        params.emplace_back(pname, InlineFunctions(pexpr));
    }

    auto const& loc = caller->GetLocation();
    if (partial_inline_) {
        // retain call node
        return std::make_shared<CallNode>(
            loc, ShallowCopy(builtin), std::move(params));
    }

    // return builtin function result
    auto const& bt_name = builtin->GetName();
    auto result = CallBuiltinWithParams(bt_name, loc, params);

    if (bt_name == "env") {
        // Built-in function env() emits a runtime variable reference (VarNode).
        // Those are typically conflict free, except for rare cases. One such
        // case is the iteration variable of a runtime-evaluated expression
        // (foreach,foldl), which is added to the scope as a runtime variable.
        // (see ASTInlineVisitor::operator()(ForEachNode const*))
        // Example:
        //     "[ foo + jst.env('foo') for foo in jst.env('range') ]"
        // Due to range not being statically known, the iteration variable foo
        // will become a runtime variable, potentially shadowing the environment
        // variable referred to via jst.env('foo'). The following code checks
        // for this scenario and throws an error to avoid unexpected behavior.
        auto const* var = ASTNode::Cast<VarNode const*>(result.GetNode().get());
        if (var == nullptr) {
            throw ASTInlineError{
                "Expected var node from env() built-in function.", loc};
        }
        auto const& var_name = var->GetName();
        auto const* var_def = scope_->GetDef(var_name);
        if (var_def != nullptr and
            ASTNode::Cast<VarNode const*>(var_def->get()) != nullptr) {
            throw ASTInlineError{"Config variable '" + var_name +
                                     "' is shadowed by scope variable.",
                                 loc};
        }
    }

    if (result.NeedsInlining()) {
        // control-flow nodes such as ForEach or FoldLeft may need inlining
        return InlineFunctions(result.GetNode());
    }
    return result.GetNode();
}

}  // namespace justlang
