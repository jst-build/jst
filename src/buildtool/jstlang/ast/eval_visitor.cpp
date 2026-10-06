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
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/builtin_functions.hpp"
#include "src/buildtool/jstlang/ast/scope.hpp"

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

[[nodiscard]] auto GetLocationString(jstlang::Location const& loc)
    -> std::string {
    std::ostringstream oss{};
    oss << loc.File();
    oss << ":" << loc.line;
    oss << ":" << loc.column;
    if (loc.column_end > 0) {
        oss << "-" << loc.column_end;
    }
    return oss.str();
}

[[nodiscard]] auto GetLocationSnippet(jstlang::Location const& loc)
    -> std::string {
    std::ostringstream oss{};
    if (loc.source != nullptr) {
        auto const& content = loc.source->content;
        auto line_pos = 0UL;
        auto line_end = content.find('\n', line_pos);
        for (std::size_t i{1}; i < loc.line and line_end != std::string::npos;
             ++i) {
            line_pos = line_end + 1;
            line_end = content.find('\n', line_pos);
        }
        if (line_end != std::string::npos) {
            static auto const kPrefix = " |" + std::string(2, ' ');
            auto line_str = std::to_string(loc.line);
            auto line_data = content.substr(line_pos, line_end - line_pos);
            auto const column_end =
                loc.column_end > 0 ? loc.column_end : line_data.length();
            oss << "\n\n";
            oss << line_str + kPrefix;
            oss << line_data;
            oss << "\n";
            oss << std::string(line_str.length(), ' ') + kPrefix;
            oss << std::string(loc.column - 1, ' ');
            oss << std::string(column_end - loc.column + 1, '^');
            oss << "\n";
        }
    }
    return oss.str();
}

}  // namespace

namespace jstlang {

ASTEvalError::ASTEvalError(std::string const& error) : runtime_error{error} {}

ASTEvalError::ASTEvalError(std::string const& error,
                           jstlang::Location const& loc)
    : runtime_error{GetLocationString(loc) + ": " + error +
                    GetLocationSnippet(loc)} {}

auto ASTEvalVisitor::CallBuiltinWithParams(std::string const& name,
                                           Location const& loc,
                                           CallNode::params_t const& params)
    -> ASTNodePtr {
    auto bt_it = kBuiltinFunctions.find(name);
    if (bt_it == kBuiltinFunctions.end()) {
        throw ASTEvalError{"Unknown builtin function \"" + name + "\".", loc};
    }
    auto eval_callback = [](ASTNodePtr const& node,
                            ScopePtr const& scope) -> ASTNodePtr {
        return WithScope(scope).Evaluate(node);
    };
    try {
        return std::invoke(bt_it->second, loc, eval_callback, params);
    } catch (std::exception const& e) {
        throw ASTEvalError{e.what()};
    }
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
                           Location const& loc) -> ASTNodePtr {
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
            return ApplyBuiltin(*node, *builtin);
        }
        if (auto const* closure =
                ASTNode::Cast<ClosureNode const*>(value.get())) {
            return Apply(*node, *closure);
        }
        throw ASTEvalError{"Call target is not a function.",
                           node->GetLocation()};
    }

    if (std::holds_alternative<CallNode::builtin_t>(target)) {
        return ApplyBuiltin(*node, *std::get<CallNode::builtin_t>(target));
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
            return ApplyBuiltin(*node, *builtin);
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
    // foreign AST is self-contained, evaluate in clean scope
    auto ast = ProvideBuiltinsToAST(node->GetExpr());
    return WithScope(nullptr).Evaluate(ast);
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
    if (not changed) {
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
    if (not changed) {
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
    if (not changed) {
        return self;
    }
    return std::make_shared<MapNode>(
        node->GetLocation(),
        std::move(values),
        has_new_literal_names ? MapNode::Unique::Maybe : MapNode::Unique::Yes);
}

auto ASTEvalVisitor::operator()(VarNode const* /*node*/,
                                ASTNodePtr const& self) const -> ASTNodePtr {
    // runtime variable, value unknown
    return self;
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

    if (auto const* list = ASTNode::Cast<ListNode const*>(range.get())) {
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

    // Evaluate the body with the iteration variable as runtime variable (with
    // the same name), evaluated during runtime.
    auto body_scope = Scope::BindValue(
        scope_, var_name, std::make_shared<VarNode>(loc, var_name));
    return std::make_shared<ForEachNode>(
        loc,
        var_name,
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
    if (list1 != nullptr and list2 != nullptr) {
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

    // Evaluate the body with the iteration variables as runtime variables
    // (with the same name), evaluated during runtime.
    auto const& loc = node->GetLocation();
    auto body_scope = Scope::BindValue(
        scope_, var1_name, std::make_shared<VarNode>(loc, var1_name));
    body_scope = Scope::BindValue(std::move(body_scope),
                                  var2_name,
                                  std::make_shared<VarNode>(loc, var2_name));
    return std::make_shared<ZipWithNode>(
        loc,
        var1_name,
        var2_name,
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

    if (auto const* list = ASTNode::Cast<ListNode const*>(range.get())) {
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

    // Evaluate the body with the iteration and accumulation variables as
    // runtime variables (with the same name), evaluated during runtime.
    auto const& loc = node->GetLocation();
    auto body_scope = Scope::BindValue(
        scope_, iter_var_name, std::make_shared<VarNode>(loc, iter_var_name));
    body_scope =
        Scope::BindValue(std::move(body_scope),
                         accu_var_name,
                         std::make_shared<VarNode>(loc, accu_var_name));
    return std::make_shared<FoldLeftNode>(
        loc,
        iter_var_name,
        accu_var_name,
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
    auto lhs = Evaluate(node->GetLhs());
    auto rhs = Evaluate(node->GetRhs());

    // resolve BinaryOperationNode
    auto const& loc = node->GetLocation();
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
        throw ASTEvalError{
            std::string{"Inlining function call failed with:\n"} + e.what(),
            caller.GetLocation()};
    }
}

auto ASTEvalVisitor::ApplyBuiltin(CallNode const& caller,
                                  BuiltinNode const& builtin) const
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
    auto params = CallNode::params_t{};
    params.reserve(caller_params->size());
    for (auto const& [pname, pexpr] : *caller_params) {
        params.emplace_back(pname, Evaluate(pexpr));
    }

    // return builtin function result
    auto const& loc = caller.GetLocation();
    auto const& bt_name = builtin.GetName();
    auto result = CallBuiltinWithParams(bt_name, loc, params);

    if (bt_name == "env") {
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
