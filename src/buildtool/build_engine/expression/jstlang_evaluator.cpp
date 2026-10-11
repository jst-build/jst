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

#include <exception>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/jstlang/ast/eval_error.hpp"

using jstlang::AnonTargetNode;
using jstlang::ArtifactNode;
using jstlang::ASTEvalError;
using jstlang::ASTNode;
using jstlang::ASTNodePtr;
using jstlang::Bindings;
using jstlang::BoolNode;
using jstlang::BuiltinCallNode;
using jstlang::BuiltinCallToJson;
using jstlang::EntityNameNode;
using jstlang::EvalOptions;
using jstlang::ListNode;
using jstlang::Location;
using jstlang::MapNode;
using jstlang::NodeKind;
using jstlang::NullNode;
using jstlang::NumberNode;
using jstlang::OpaqueValue;
using jstlang::RequireValue;
using jstlang::ResultNode;
using jstlang::StringNode;
using jstlang::ValueToJson;
using jstlang::VarNode;

namespace {

class EvalError final : public std::runtime_error {
  public:
    explicit EvalError(std::string const& msg) : runtime_error{msg} {}
};

/// \brief Value of the expression language that is opaque to jstlang (e.g.,
/// artifacts, results, target names).
class ExpressionValue final : public OpaqueValue {
  public:
    explicit ExpressionValue(ExpressionPtr value) noexcept
        : value_{std::move(value)} {}

    [[nodiscard]] auto Value() const noexcept -> ExpressionPtr const& {
        return value_;
    }

    [[nodiscard]] auto ToJson() const -> nlohmann::json final {
        return value_->ToJson(Expression::JsonMode::NullForNonJson);
    }

    [[nodiscard]] auto Identity() const -> std::string final {
        // hex, as identities are used in JSON (raw hashes are not UTF-8)
        return value_->ToIdentifier();
    }

    [[nodiscard]] auto IsComparable() const -> bool final {
        return value_->IsCacheable();
    }

    [[nodiscard]] auto ToString() const -> std::string final {
        return value_->ToAbbrevString(Evaluator::GetExpressionLogLimit());
    }

  private:
    ExpressionPtr value_;
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
        auto list = std::make_shared<ListNode>(loc, std::move(items));
        list->SetOrigin(std::make_shared<ExpressionValue const>(value));
        return list;
    }
    if (value->IsMap()) {
        auto fields = MapNode::fields_t{};
        fields.reserve(value->Map().size());
        for (auto const& [key, item] : value->Map()) {
            fields.emplace_back(std::make_shared<StringNode>(loc, key),
                                FromExpression(item));
        }
        auto map = std::make_shared<MapNode>(loc, std::move(fields));
        map->SetOrigin(std::make_shared<ExpressionValue const>(value));
        return map;
    }
    auto opaque = std::make_shared<ExpressionValue const>(value);
    if (value->IsArtifact()) {
        return std::make_shared<ArtifactNode>(loc, std::move(opaque));
    }
    if (value->IsResult()) {
        return std::make_shared<ResultNode>(loc, std::move(opaque));
    }
    if (value->IsNode()) {
        return std::make_shared<AnonTargetNode>(loc, std::move(opaque));
    }
    if (value->IsName()) {
        return std::make_shared<EntityNameNode>(loc, std::move(opaque));
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
            auto const* node = ASTNode::Cast<ListNode const*>(value.get());
            if (auto const* origin = node->GetOrigin()) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
                return static_cast<ExpressionValue const&>(*origin).Value();
            }
            auto const& items = node->GetItems();
            if (items.empty()) {
                // per thread, so its hash is computed once
                thread_local ExpressionPtr const kEmptyList{
                    Expression::list_t{}};
                return kEmptyList;
            }
            auto list = Expression::list_t{};
            list.reserve(items.size());
            for (auto const& item : items) {
                list.emplace_back(ToExpression(item));
            }
            auto result = ExpressionPtr{std::move(list)};
            // converting the node again returns the same value (and hash)
            node->SetOrigin(std::make_shared<ExpressionValue const>(result));
            return result;
        }
        case NodeKind::Map: {
            auto const* node = ASTNode::Cast<MapNode const*>(value.get());
            if (auto const* origin = node->GetOrigin()) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
                return static_cast<ExpressionValue const&>(*origin).Value();
            }
            auto map = Expression::map_t::underlying_map_t{};
            for (auto const& [key, item] : node->GetFields()) {
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
            if (map.empty()) {
                // per thread, so its hash is computed once
                thread_local ExpressionPtr const kEmptyMap{
                    Expression::map_t{Expression::map_t::underlying_map_t{}}};
                return kEmptyMap;
            }
            auto result = ExpressionPtr{Expression::map_t{std::move(map)}};
            // converting the node again returns the same value (and hash)
            node->SetOrigin(std::make_shared<ExpressionValue const>(result));
            return result;
        }
        case NodeKind::Artifact:
        case NodeKind::Result:
        case NodeKind::AnonTarget:
        case NodeKind::EntityName: {
            // only values of the expression language exist
            auto const& opaque =
                jstlang::ExternalValueNode::From(value.get())->GetValue();
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
            return static_cast<ExpressionValue const&>(*opaque).Value();
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

/// \brief Rule-only built-in functions, evaluated by the expression evaluator
/// with the functions of the rule analysis: the expression lowering would
/// generate for a call (cached per shape of the call) is evaluated with the
/// evaluated arguments being runtime variables.
class RuleFunctions final {
  public:
    RuleFunctions(FunctionMapPtr functions,
                  JstlangEvaluator::Annotator const& annotate_object,
                  JstlangEvaluator::NoteUserContext const& note_user_context)
        : functions_{std::move(functions)},
          annotate_object_{annotate_object},
          note_user_context_{note_user_context} {}

    [[nodiscard]] auto Call(BuiltinCallNode const& call) const -> ASTNodePtr {
        auto const& name = call.GetName();
        auto const& loc = call.GetLocation();
        auto key = name;
        auto args = BuiltinCallNode::args_t{};
        auto env = Expression::map_t::underlying_map_t{};
        ASTNodePtr default_value{};
        args.reserve(call.GetArgs().size());
        for (std::size_t pos{}; pos < call.GetArgs().size(); ++pos) {
            auto const& value = call.GetArgs()[pos];
            if (value == nullptr) {
                key += "|-";
                args.emplace_back(nullptr);
                continue;
            }
            if (IsLiteralArgument(name, pos)) {
                // not evaluated by the expression evaluator
                key += "|=" + ValueToJson(*value).dump();
                args.emplace_back(value);
                continue;
            }
            key += "|#";
            auto var = "#" + std::to_string(pos);
            args.emplace_back(std::make_shared<VarNode>(loc, var));
            if (IsDefaultArgument(name, pos)) {
                // only selected if provider is missing
                default_value = value;
                env.emplace(std::move(var), DefaultSentinel());
            }
            else {
                env.emplace(std::move(var), ToExpression(value));
            }
        }

        auto expr = CachedExpression(
            key, BuiltinCallNode{loc, name, std::move(args), call.EvalType()});
        auto error = std::string{};
        auto result = ::Evaluator::EvaluateExpression(
            expr,
            Configuration{Expression::map_t{std::move(env)}},
            functions_,
            [&error](std::string const& msg) { error += msg; },
            annotate_object_,
            note_user_context_);
        if (not result) {
            throw ASTEvalError{
                "Evaluating " + name + "() failed with:", loc, error};
        }
        if (default_value and result == DefaultSentinel()) {
            return default_value;
        }
        return FromExpression(result);
    }

  private:
    FunctionMapPtr functions_;
    JstlangEvaluator::Annotator const& annotate_object_;
    JstlangEvaluator::NoteUserContext const& note_user_context_;

    [[nodiscard]] static auto DefaultSentinel() -> ExpressionPtr const& {
        static auto const kSentinel =
            ExpressionPtr{std::string{"\0jstlang:default", 16}};
        return kSentinel;
    }

    /// \brief Whether the argument at a parameter position is the default
    /// value of dep_provides().
    [[nodiscard]] static auto IsDefaultArgument(std::string const& name,
                                                std::size_t pos) -> bool {
        static constexpr std::size_t kDefaultPos = 3;
        return name == "dep_provides" and pos == kDefaultPos;
    }

    /// \brief Whether the argument at a parameter position is not evaluated
    /// by the expression evaluator (disjoint of tree_overlay(), may_fail and
    /// no_cache of action()).
    [[nodiscard]] static auto IsLiteralArgument(std::string const& name,
                                                std::size_t pos) -> bool {
        static constexpr std::size_t kMayFailPos = 7;
        static constexpr std::size_t kNoCachePos = 9;
        return (name == "tree_overlay" and pos == 1) or
               (name == "action" and
                (pos == kMayFailPos or pos == kNoCachePos));
    }

    [[nodiscard]] static auto CachedExpression(std::string const& key,
                                               BuiltinCallNode const& call)
        -> ExpressionPtr {
        static std::mutex mutex{};
        static std::unordered_map<std::string, ExpressionPtr> cache{};
        {
            std::unique_lock lock{mutex};
            if (auto it = cache.find(key); it != cache.end()) {
                return it->second;
            }
        }
        auto expr = Expression::FromJson(BuiltinCallToJson(call));
        std::unique_lock lock{mutex};
        cache.emplace(key, expr);
        return expr;
    }
};

/// \brief Full evaluation of an expression in a configuration (or static, if
/// env is nullptr), resulting in a value that may contain residual fail().
[[nodiscard]] auto EvaluateValue(
    ASTNodePtr const& expr,
    Bindings::Ptr const& bindings,
    Configuration const* env,
    EvalOptions::ExternalBuiltin const* external = nullptr) -> ASTNodePtr {
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
                               .config = env != nullptr ? &lookup : nullptr,
                               .external = external};
    return jstlang::Evaluator{options, bindings}.EvaluateAST(expr);
}

/// \brief Value of the structure of an expression: unknown values stay
/// residual code (unfolding), i.e., entries are self-contained.
[[nodiscard]] auto EvaluateStructure(ASTNodePtr const& expr,
                                     Bindings::Ptr const& bindings)
    -> ASTNodePtr {
    return jstlang::Evaluator{EvalOptions{.mode = EvalOptions::Mode::Unfold},
                              bindings}
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
                                    IsSelfContained(*entry) ? entry : nullptr,
                                    /*allow_unfold=*/true));
    }
    return fields;
}

/// \brief Fields of a map in source code (literal names), whose values are
/// evaluated separately.
[[nodiscard]] auto FieldsOfMapSyntax(MapNode const& map,
                                     Bindings::Ptr const& bindings,
                                     bool allow_unfold)
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
                                    value->IsLiteral() ? value : nullptr,
                                    allow_unfold));
    }
    return fields;
}

/// \brief Description of a value involved in an error, or the empty string
/// if the value cannot be described (e.g., a function or unevaluated code).
[[nodiscard]] auto DescribeInvolvedValue(
    ASTNodePtr const& value,
    JstlangEvaluator::Annotator const& annotate) noexcept -> std::string {
    try {
        return annotate(ToExpression(value));
    } catch (...) {
        return std::string{};
    }
}

/// \brief Message of an error, followed by the description of the values
/// involved in it (e.g., where conflicting artifacts come from).
[[nodiscard]] auto ErrorMessage(ASTEvalError const& e,
                                JstlangEvaluator::Annotator const& annotate)
    -> std::string {
    auto message = std::string{e.what()};
    for (auto const& value : e.InvolvedValues()) {
        message += DescribeInvolvedValue(value, annotate);
    }
    return message;
}

}  // namespace

auto JstlangEvaluator::Evaluate(
    ASTNodePtr const& expr,
    Bindings::Ptr const& bindings,
    Configuration const* env,
    FunctionMapPtr const& functions,
    Logger const& logger,
    Annotator const& annotate_object,
    NoteUserContext const& note_user_context) noexcept -> ExpressionPtr {
    try {
        auto const rule_functions =
            RuleFunctions{functions, annotate_object, note_user_context};
        auto const external = EvalOptions::ExternalBuiltin{
            [&rule_functions](BuiltinCallNode const& call) {
                return rule_functions.Call(call);
            }};
        auto value =
            EvaluateValue(expr, bindings, env, functions ? &external : nullptr);
        RequireValue(value);
        return ToExpression(value);
    } catch (ASTEvalError const& e) {
        logger(ErrorMessage(e, annotate_object));
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return ExpressionPtr{nullptr};
}

auto JstlangEvaluator::Evaluate(
    FieldPtr const& field,
    Configuration const* env,
    FunctionMapPtr const& functions,
    Logger const& logger,
    Annotator const& annotate_object,
    NoteUserContext const& note_user_context) noexcept -> ExpressionPtr {
    try {
        auto const& static_value = field->StaticValue();
        if (static_value and static_value->IsLiteral()) {
            return ToExpression(static_value);
        }
        if (static_value) {
            // self-contained residual code of unfolding
            return Evaluate(static_value,
                            Bindings::Empty(),
                            env,
                            functions,
                            logger,
                            annotate_object,
                            note_user_context);
        }
        if (field->Path().empty()) {
            return Evaluate(field->Expr(),
                            field->GetBindings(),
                            env,
                            functions,
                            logger,
                            annotate_object,
                            note_user_context);
        }
        auto const rule_functions =
            RuleFunctions{functions, annotate_object, note_user_context};
        auto const external = EvalOptions::ExternalBuiltin{
            [&rule_functions](BuiltinCallNode const& call) {
                return rule_functions.Call(call);
            }};
        auto value = EvaluateValue(field->Expr(),
                                   field->GetBindings(),
                                   env,
                                   functions ? &external : nullptr);
        for (auto const& name : field->Path()) {
            value = SelectEntry(value, name);
            if (not value) {
                throw EvalError{"Field " + name + " is missing."};
            }
        }
        RequireValue(value);
        return ToExpression(value);
    } catch (ASTEvalError const& e) {
        logger(ErrorMessage(e, annotate_object));
    } catch (std::exception const& e) {
        logger(e.what());
    }
    return ExpressionPtr{nullptr};
}

auto JstlangEvaluator::EvaluateFields(ASTNodePtr const& expr,
                                      Bindings::Ptr const& bindings,
                                      bool allow_unfold,
                                      Logger const& logger) noexcept
    -> std::optional<Fields> {
    try {
        if (auto const* map = ASTNode::Cast<MapNode const*>(expr.get())) {
            if (auto fields = FieldsOfMapSyntax(*map, bindings, allow_unfold)) {
                return fields;
            }
        }
        if (not allow_unfold) {
            throw EvalError{"Expected a literal map with literal field names."};
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
        return EvaluateFields(
            field->Expr(), field->GetBindings(), field->AllowUnfold(), logger);
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
