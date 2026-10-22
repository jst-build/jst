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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_TRAVERSAL_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_TRAVERSAL_HPP

#include <memory>
#include <optional>
#include <utility>
#include <variant>

#include "src/buildtool/jstlang/ast/ast.hpp"

/// \brief Generic traversal of AST nodes. This is the only place that knows
/// the children of each node kind.
namespace jstlang {

/// \brief Call f(child) for each child of the node that is not null, in order
/// of definition. Children are expressions (ASTNodePtr const&).
template <class TFunc>
void ForEachChild(ASTNode const& node, TFunc const& f) {
    auto visit = [&f](ASTNodePtr const& child) {
        if (child) {
            f(child);
        }
    };
    switch (node.Kind()) {
        case NodeKind::Let: {
            auto const* let = ASTNode::Cast<LetNode const*>(&node);
            visit(let->GetValue());
            visit(let->GetNext());
        } break;
        case NodeKind::Func: {
            auto const* func = ASTNode::Cast<FuncNode const*>(&node);
            for (auto const& [_, default_expr] : func->GetParams()) {
                visit(default_expr);
            }
            visit(func->GetBody());
        } break;
        case NodeKind::BuiltinCall: {
            auto const* call = ASTNode::Cast<BuiltinCallNode const*>(&node);
            for (auto const& arg : call->GetArgs()) {
                visit(arg);
            }
        } break;
        case NodeKind::Call: {
            auto const* call = ASTNode::Cast<CallNode const*>(&node);
            if (auto const* target =
                    std::get_if<CallNode::expr_t>(&call->GetTarget())) {
                visit(*target);
            }
            if (auto const& params = call->GetParams()) {
                for (auto const& [_, arg] : *params) {
                    visit(arg);
                }
            }
        } break;
        case NodeKind::Foreign:
            visit(ASTNode::Cast<ForeignNode const*>(&node)->GetExpr());
            break;
        case NodeKind::List:
            for (auto const& item :
                 ASTNode::Cast<ListNode const*>(&node)->GetItems()) {
                visit(item);
            }
            break;
        case NodeKind::Map:
            for (auto const& [key, value] :
                 ASTNode::Cast<MapNode const*>(&node)->GetFields()) {
                visit(key);
                visit(value);
            }
            break;
        case NodeKind::Var:
            visit(ASTNode::Cast<VarNode const*>(&node)->GetDefault());
            break;
        case NodeKind::If: {
            auto const* if_node = ASTNode::Cast<IfNode const*>(&node);
            visit(if_node->GetCondition());
            visit(if_node->GetThenBranch());
            visit(if_node->GetElseBranch());
        } break;
        case NodeKind::ForEach: {
            auto const* loop = ASTNode::Cast<ForEachNode const*>(&node);
            visit(loop->GetRange());
            visit(loop->GetBody());
        } break;
        case NodeKind::ZipWith: {
            auto const* zip = ASTNode::Cast<ZipWithNode const*>(&node);
            visit(zip->GetRange1());
            visit(zip->GetRange2());
            visit(zip->GetBody());
        } break;
        case NodeKind::FoldLeft: {
            auto const* fold = ASTNode::Cast<FoldLeftNode const*>(&node);
            visit(fold->GetInit());
            visit(fold->GetRange());
            visit(fold->GetBody());
        } break;
        case NodeKind::UnaryOperation:
            visit(ASTNode::Cast<UnaryOperationNode const*>(&node)
                      ->GetExpression());
            break;
        case NodeKind::Lookup: {
            auto const* lookup = ASTNode::Cast<LookupNode const*>(&node);
            visit(lookup->GetIndex());
            visit(lookup->GetContainer());
            visit(lookup->GetDefault());
        } break;
        case NodeKind::BinaryOperation: {
            auto const* op = ASTNode::Cast<BinaryOperationNode const*>(&node);
            visit(op->GetLhs());
            visit(op->GetRhs());
        } break;
        case NodeKind::Builtin:
        case NodeKind::Bool:
        case NodeKind::String:
        case NodeKind::Number:
        case NodeKind::Null:
        case NodeKind::Ref:
        case NodeKind::Closure:
        case NodeKind::Artifact:
        case NodeKind::Result:
        case NodeKind::AnonTarget:
        case NodeKind::EntityName:
            break;  // leaf nodes
    }
}

/// \brief Rebuild the node with each child that is not null replaced by
/// f(child). If f returns the same pointer for all children, the node itself
/// is returned (no allocation), so unchanged subtrees keep their identity.
template <class TFunc>
[[nodiscard]] auto MapChildren(ASTNodePtr const& node,
                               TFunc const& f) -> ASTNodePtr {
    bool changed{};
    auto map = [&f, &changed](ASTNodePtr const& child) -> ASTNodePtr {
        if (not child) {
            return child;
        }
        auto mapped = f(child);
        changed = changed or mapped != child;
        return mapped;
    };
    auto const& loc = node->GetLocation();
    switch (node->Kind()) {
        case NodeKind::Let: {
            auto const* let = ASTNode::Cast<LetNode const*>(node.get());
            auto value = map(let->GetValue());
            auto next = map(let->GetNext());
            if (changed) {
                return std::make_shared<LetNode>(
                    loc, let->GetName(), std::move(value), std::move(next));
            }
        } break;
        case NodeKind::Func: {
            auto const* func = ASTNode::Cast<FuncNode const*>(node.get());
            auto params = FuncNode::params_t{};
            params.reserve(func->GetParams().size());
            for (auto const& [name, default_expr] : func->GetParams()) {
                params.emplace_back(name, map(default_expr));
            }
            auto body = map(func->GetBody());
            if (changed) {
                return std::make_shared<FuncNode>(
                    loc, std::move(params), std::move(body));
            }
        } break;
        case NodeKind::BuiltinCall: {
            auto const* call =
                ASTNode::Cast<BuiltinCallNode const*>(node.get());
            auto args = BuiltinCallNode::args_t{};
            args.reserve(call->GetArgs().size());
            for (auto const& arg : call->GetArgs()) {
                args.emplace_back(map(arg));
            }
            if (changed) {
                return std::make_shared<BuiltinCallNode>(
                    loc, call->GetName(), std::move(args), call->EvalType());
            }
        } break;
        case NodeKind::Call: {
            auto const* call = ASTNode::Cast<CallNode const*>(node.get());
            auto target = call->GetTarget();
            if (auto* expr = std::get_if<CallNode::expr_t>(&target)) {
                *expr = map(*expr);
            }
            auto params = std::optional<CallNode::params_t>{};
            if (auto const& call_params = call->GetParams()) {
                params.emplace();
                params->reserve(call_params->size());
                for (auto const& [name, arg] : *call_params) {
                    params->emplace_back(name, map(arg));
                }
            }
            if (changed) {
                return std::make_shared<CallNode>(
                    loc, std::move(target), std::move(params));
            }
        } break;
        case NodeKind::Foreign: {
            auto expr =
                map(ASTNode::Cast<ForeignNode const*>(node.get())->GetExpr());
            if (changed) {
                return std::make_shared<ForeignNode>(loc, std::move(expr));
            }
        } break;
        case NodeKind::List: {
            auto const& items =
                ASTNode::Cast<ListNode const*>(node.get())->GetItems();
            auto mapped = ListNode::items_t{};
            mapped.reserve(items.size());
            for (auto const& item : items) {
                mapped.emplace_back(map(item));
            }
            if (changed) {
                return std::make_shared<ListNode>(loc, std::move(mapped));
            }
        } break;
        case NodeKind::Map: {
            auto const& fields =
                ASTNode::Cast<MapNode const*>(node.get())->GetFields();
            auto mapped = MapNode::fields_t{};
            mapped.reserve(fields.size());
            for (auto const& [key, value] : fields) {
                auto mapped_key = map(key);
                mapped.emplace_back(std::move(mapped_key), map(value));
            }
            if (changed) {
                return std::make_shared<MapNode>(loc, std::move(mapped));
            }
        } break;
        case NodeKind::Var: {
            auto const* var = ASTNode::Cast<VarNode const*>(node.get());
            auto default_expr = map(var->GetDefault());
            if (changed) {
                return std::make_shared<VarNode>(
                    loc, var->GetName(), std::move(default_expr));
            }
        } break;
        case NodeKind::If: {
            auto const* if_node = ASTNode::Cast<IfNode const*>(node.get());
            auto cond = map(if_node->GetCondition());
            auto then_expr = map(if_node->GetThenBranch());
            auto else_expr = map(if_node->GetElseBranch());
            if (changed) {
                return std::make_shared<IfNode>(loc,
                                                std::move(cond),
                                                std::move(then_expr),
                                                std::move(else_expr));
            }
        } break;
        case NodeKind::ForEach: {
            auto const* loop = ASTNode::Cast<ForEachNode const*>(node.get());
            auto range = map(loop->GetRange());
            auto body = map(loop->GetBody());
            if (changed) {
                return std::make_shared<ForEachNode>(loc,
                                                     loop->GetVariable(),
                                                     std::move(range),
                                                     std::move(body));
            }
        } break;
        case NodeKind::ZipWith: {
            auto const* zip = ASTNode::Cast<ZipWithNode const*>(node.get());
            auto range1 = map(zip->GetRange1());
            auto range2 = map(zip->GetRange2());
            auto body = map(zip->GetBody());
            if (changed) {
                return std::make_shared<ZipWithNode>(loc,
                                                     zip->GetVariable1(),
                                                     zip->GetVariable2(),
                                                     std::move(range1),
                                                     std::move(range2),
                                                     std::move(body));
            }
        } break;
        case NodeKind::FoldLeft: {
            auto const* fold = ASTNode::Cast<FoldLeftNode const*>(node.get());
            auto init = map(fold->GetInit());
            auto range = map(fold->GetRange());
            auto body = map(fold->GetBody());
            if (changed) {
                return std::make_shared<FoldLeftNode>(loc,
                                                      fold->GetIterVar(),
                                                      fold->GetAccuVar(),
                                                      std::move(init),
                                                      std::move(range),
                                                      std::move(body));
            }
        } break;
        case NodeKind::UnaryOperation: {
            auto const* op =
                ASTNode::Cast<UnaryOperationNode const*>(node.get());
            auto expr = map(op->GetExpression());
            if (changed) {
                return std::make_shared<UnaryOperationNode>(
                    loc, op->GetType(), std::move(expr), op->EvalType());
            }
        } break;
        case NodeKind::Lookup: {
            auto const* lookup = ASTNode::Cast<LookupNode const*>(node.get());
            auto index = map(lookup->GetIndex());
            auto container = map(lookup->GetContainer());
            auto default_expr = map(lookup->GetDefault());
            if (changed) {
                return std::make_shared<LookupNode>(loc,
                                                    std::move(index),
                                                    std::move(container),
                                                    std::move(default_expr));
            }
        } break;
        case NodeKind::BinaryOperation: {
            auto const* op =
                ASTNode::Cast<BinaryOperationNode const*>(node.get());
            auto lhs = map(op->GetLhs());
            auto rhs = map(op->GetRhs());
            if (changed) {
                return std::make_shared<BinaryOperationNode>(loc,
                                                             op->GetType(),
                                                             std::move(lhs),
                                                             std::move(rhs),
                                                             op->EvalType());
            }
        } break;
        case NodeKind::Builtin:
        case NodeKind::Bool:
        case NodeKind::String:
        case NodeKind::Number:
        case NodeKind::Null:
        case NodeKind::Ref:
        case NodeKind::Closure:
        case NodeKind::Artifact:
        case NodeKind::Result:
        case NodeKind::AnonTarget:
        case NodeKind::EntityName:
            break;  // leaf nodes
    }
    return node;
}

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_TRAVERSAL_HPP
