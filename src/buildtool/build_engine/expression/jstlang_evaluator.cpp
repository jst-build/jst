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

#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"

#include <optional>
#include <ranges>
#include <stdexcept>
#include <unordered_map>

#include "src/buildtool/build_engine/expression/expression.hpp"

using jstlang::ASTNode;
using jstlang::ASTNodePtr;
using jstlang::Bindings;
using jstlang::BoolNode;
using jstlang::EvalOptions;
using jstlang::Evaluator;
using jstlang::ListNode;
using jstlang::Location;
using jstlang::MapNode;
using jstlang::NodeKind;
using jstlang::NullNode;
using jstlang::NumberNode;
using jstlang::RequireValue;
using jstlang::StringNode;
using jstlang::ValueToJson;

namespace {

class EvalError final : public std::runtime_error {
  public:
    explicit EvalError(std::string const& msg) : runtime_error{msg} {}
};

/// \brief Convert value of the expression language to a literal AST.
[[nodiscard]] auto FromExpression(ExpressionPtr const& value) -> ASTNodePtr {
    auto const loc = Location{};
    if (value->IsNone()) {
        return std::make_shared<NullNode>(loc);
    }
    if (value->IsBool()) {
        return std::make_shared<BoolNode>(loc, value->Bool());
    }
    if (value->IsNumber()) {
        return std::make_shared<NumberNode>(loc, value->Number());
    }
    if (value->IsString()) {
        return std::make_shared<StringNode>(loc, value->String());
    }
    if (value->IsList()) {
        auto items = ListNode::items_t{};
        items.reserve(value->List().size());
        for (auto const& item : value->List()) {
            items.emplace_back(FromExpression(item));
        }
        return std::make_shared<ListNode>(loc, std::move(items));
    }
    if (value->IsMap()) {
        auto fields = MapNode::fields_t{};
        fields.reserve(value->Map().size());
        for (auto const& [key, item] : value->Map()) {
            fields.emplace_back(std::make_shared<StringNode>(loc, key),
                                FromExpression(item));
        }
        return std::make_shared<MapNode>(loc, std::move(fields));
    }
    throw EvalError{"Unsupported value " + value->ToString() + "."};
}

[[nodiscard]] auto IsFunction(ASTNode const& node) -> bool {
    return node.Kind() == NodeKind::Closure or
           node.Kind() == NodeKind::Builtin or node.Kind() == NodeKind::Func;
}

/// \brief Convert a value (without residual code) to a value of the
/// expression language. Fields of maps that are functions are omitted.
[[nodiscard]] auto ToExpression(ASTNodePtr const& value) -> ExpressionPtr {
    switch (value->Kind()) {
        case NodeKind::Null:
            return ExpressionPtr{Expression::none_t{}};
        case NodeKind::Bool:
            return ExpressionPtr{
                ASTNode::Cast<BoolNode const*>(value.get())->GetValue()};
        case NodeKind::Number:
            return ExpressionPtr{
                ASTNode::Cast<NumberNode const*>(value.get())->GetValue()};
        case NodeKind::String:
            return ExpressionPtr{
                ASTNode::Cast<StringNode const*>(value.get())->GetValue()};
        case NodeKind::Ref:
            return Expression::FromJson(ValueToJson(*value));
        case NodeKind::List: {
            auto const& items =
                ASTNode::Cast<ListNode const*>(value.get())->GetItems();
            auto list = Expression::list_t{};
            list.reserve(items.size());
            for (auto const& item : items) {
                list.emplace_back(ToExpression(item));
            }
            return ExpressionPtr{std::move(list)};
        }
        case NodeKind::Map: {
            auto map = Expression::map_t::underlying_map_t{};
            for (auto const& [key, item] :
                 ASTNode::Cast<MapNode const*>(value.get())->GetFields()) {
                if (IsFunction(*item)) {
                    continue;  // functions are not values of fields
                }
                auto const* name = ASTNode::Cast<StringNode const*>(key.get());
                if (name == nullptr) {
                    throw EvalError{"Expected field names to be strings."};
                }
                // latest field wins
                map.insert_or_assign(name->GetValue(), ToExpression(item));
            }
            return ExpressionPtr{Expression::map_t{map}};
        }
        default:
            break;
    }
    if (IsFunction(*value)) {
        throw EvalError{"Expected a value, but found a function."};
    }
    throw EvalError{"Expected a value, but found unevaluated code."};
}

/// \brief Select entry of a map value (nullptr if missing).
[[nodiscard]] auto SelectEntry(ASTNodePtr const& value,
                               std::string const& name) -> ASTNodePtr {
    auto const* map = ASTNode::Cast<MapNode const*>(value.get());
    if (map == nullptr) {
        throw EvalError{"Expected a map for selecting field " + name + "."};
    }
    auto const& fields = map->GetFields();
    // latest field wins
    for (auto const& field : std::ranges::reverse_view(fields)) {
        auto const* key = ASTNode::Cast<StringNode const*>(field.first.get());
        if (key != nullptr and key->GetValue() == name) {
            return field.second;
        }
    }
    return nullptr;
}

/// \brief Full evaluation of an expression in a configuration (or static, if
/// env is nullptr), resulting in a value that may contain residual fail().
[[nodiscard]] auto EvaluateValue(ASTNodePtr const& expr,
                                 Bindings::Ptr const& bindings,
                                 Configuration const* env) -> ASTNodePtr {
    auto cache = std::unordered_map<std::string, ASTNodePtr>{};
    auto lookup = EvalOptions::ConfigLookup{
        [env, &cache](std::string const& name) -> ASTNodePtr {
            auto it = cache.find(name);
            if (it == cache.end()) {
                it = cache.emplace(name, FromExpression((*env)[name])).first;
            }
            return it->second;
        }};
    auto options = EvalOptions{.mode = EvalOptions::Mode::Full,
                               .config = env != nullptr ? &lookup : nullptr};
    return Evaluator{options, bindings}.EvaluateAST(expr);
}

/// \brief Value of the structure of an expression: unknown values stay
/// residual code (unfolding), i.e., entries are self-contained.
[[nodiscard]] auto EvaluateStructure(ASTNodePtr const& expr,
                                     Bindings::Ptr const& bindings)
    -> ASTNodePtr {
    return Evaluator{EvalOptions{.mode = EvalOptions::Mode::Unfold}, bindings}
        .EvaluateAST(expr);
}

/// \brief Fields of a map value.
[[nodiscard]] auto FieldsOfValue(ASTNodePtr const& value,
                                 ASTNodePtr const& expr,
                                 Bindings::Ptr const& bindings,
                                 std::vector<std::string> const& path)
    -> JstlangEvaluator::Fields {
    auto const* map = ASTNode::Cast<MapNode const*>(value.get());
    if (map == nullptr) {
        throw EvalError{
            "Expected a map whose structure is independent of the "
            "configuration."};
    }
    auto fields = JstlangEvaluator::Fields{};
    for (auto const& [key, entry] : map->GetFields()) {
        auto const* name = ASTNode::Cast<StringNode const*>(key.get());
        if (name == nullptr) {
            throw EvalError{
                "Expected field names to be independent of the "
                "configuration."};
        }
        auto entry_path = path;
        entry_path.emplace_back(name->GetValue());
        // latest field wins
        fields.insert_or_assign(name->GetValue(),
                                std::make_shared<JstlangEvaluator::Field const>(
                                    expr,
                                    bindings,
                                    std::move(entry_path),
                                    IsSelfContained(*entry) ? entry : nullptr));
    }
    return fields;
}

/// \brief Fields of a map in source code (literal names), whose values are
/// evaluated separately.
[[nodiscard]] auto FieldsOfMapSyntax(MapNode const& map,
                                     Bindings::Ptr const& bindings)
    -> std::optional<JstlangEvaluator::Fields> {
    auto fields = JstlangEvaluator::Fields{};
    for (auto const& [key, value] : map.GetFields()) {
        auto const* name = ASTNode::Cast<StringNode const*>(key.get());
        if (name == nullptr) {
            return std::nullopt;  // computed names
        }
        fields.insert_or_assign(name->GetValue(),
                                std::make_shared<JstlangEvaluator::Field const>(
                                    value,
                                    bindings,
                                    std::vector<std::string>{},
                                    value->IsLiteral() ? value : nullptr));
    }
    return fields;
}

}  // namespace

auto JstlangEvaluator::Evaluate(ASTNodePtr const& expr,
                                Bindings::Ptr const& bindings,
                                Configuration const* env,
                                Logger const& logger) noexcept
    -> ExpressionPtr {
    try {
        auto value = EvaluateValue(expr, bindings, env);
        RequireValue(value);
        return ToExpression(value);
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return ExpressionPtr{nullptr};
}

auto JstlangEvaluator::Evaluate(FieldPtr const& field,
                                Configuration const* env,
                                Logger const& logger) noexcept
    -> ExpressionPtr {
    try {
        auto const& static_value = field->StaticValue();
        if (static_value and static_value->IsLiteral()) {
            return ToExpression(static_value);
        }
        if (static_value) {
            // self-contained residual code of unfolding
            return Evaluate(static_value, Bindings::Empty(), env, logger);
        }
        if (field->Path().empty()) {
            return Evaluate(field->Expr(), field->GetBindings(), env, logger);
        }
        auto value = EvaluateValue(field->Expr(), field->GetBindings(), env);
        for (auto const& name : field->Path()) {
            value = SelectEntry(value, name);
            if (not value) {
                throw EvalError{"Field " + name + " is missing."};
            }
        }
        RequireValue(value);
        return ToExpression(value);
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return ExpressionPtr{nullptr};
}

auto JstlangEvaluator::EvaluateFields(ASTNodePtr const& expr,
                                      Bindings::Ptr const& bindings,
                                      Logger const& logger) noexcept
    -> std::optional<Fields> {
    try {
        if (auto const* map = ASTNode::Cast<MapNode const*>(expr.get())) {
            if (auto fields = FieldsOfMapSyntax(*map, bindings)) {
                return fields;
            }
        }
        return FieldsOfValue(EvaluateStructure(expr, bindings),
                             expr,
                             bindings,
                             std::vector<std::string>{});
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return std::nullopt;
}

auto JstlangEvaluator::EvaluateFields(FieldPtr const& field,
                                      Logger const& logger) noexcept
    -> std::optional<Fields> {
    if (field->Path().empty()) {
        return EvaluateFields(field->Expr(), field->GetBindings(), logger);
    }
    try {
        auto value = field->StaticValue();
        if (not value) {
            value = EvaluateStructure(field->Expr(), field->GetBindings());
            for (auto const& name : field->Path()) {
                value = SelectEntry(value, name);
                if (not value) {
                    throw EvalError{"Field " + name + " is missing."};
                }
            }
        }
        return FieldsOfValue(
            value, field->Expr(), field->GetBindings(), field->Path());
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return std::nullopt;
}
