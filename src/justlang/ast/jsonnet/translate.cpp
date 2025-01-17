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

#include "justlang/ast/jsonnet/translate.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <ast.h>           // jsonnet
#include <static_error.h>  // jsonnet
#include <unicode.h>       // jsonnet

#include "justlang/ast/ast.hpp"
#include "justlang/file_data.hpp"
#include "justlang/json_string.hpp"
#include "justlang/ref.hpp"

namespace {

using justlang::ASTNodePtr;
using justlang::jsonnet::ASTTranslateError;

[[nodiscard]] auto ToLocation(::jsonnet::internal::LocationRange const& loc,
                              justlang::FileLocation const& origin)
    -> justlang::Location {
    return justlang::Location{
        .file = loc.file,
        .line = loc.begin.line,
        .column = loc.begin.column,
        .column_end = loc.begin.column < loc.end.column ? loc.end.column : 0,
        .content = origin.content};
}

[[nodiscard]] auto GetLocationString(
    ::jsonnet::internal::LocationRange const& loc) -> std::string {
    std::ostringstream oss{};
    oss << loc.file;
    oss << ":" << loc.begin.line;
    if (loc.begin.line == loc.end.line) {
        // column data is only useful if begin and end is in same line
        oss << ":" << loc.begin.column;
        if (loc.begin.column != loc.end.column) {
            oss << "-" << loc.end.column;
        }
    }
    return oss.str();
}

[[nodiscard]] auto ToString(::jsonnet::internal::Var const* ast)
    -> std::string {
    if (ast->id == nullptr) {
        throw ASTTranslateError{"Missing variable identifier (name)",
                                ast->location};
    }
    return ::jsonnet::internal::encode_utf8(ast->id->name);
}

class ASTTranslator {
  public:
    explicit ASTTranslator(justlang::FileLocation input_file,
                           justlang::jsonnet::import_callback_t import_callback,
                           std::vector<justlang::FileLocation> import_chain)
        : input_file_{std::move(input_file)},
          import_callback_{std::move(import_callback)},
          import_chain_{std::move(import_chain)} {
        import_chain_.emplace_back(input_file_);
    }

    template <class T>
    [[nodiscard]] auto MakeNode(T const* ast) -> ASTNodePtr;

    [[nodiscard]] auto NodeFromJsonnet(::jsonnet::internal::AST const* expr)
        -> ASTNodePtr;

    [[nodiscard]] auto ToParams(::jsonnet::internal::ArgParams const& args)
        -> justlang::FuncNode::params_t {
        auto params = justlang::FuncNode::params_t{};
        params.reserve(args.size());
        for (auto const& param : args) {
            auto pname = ::jsonnet::internal::encode_utf8(param.id->name);
            auto pexpr = param.expr == nullptr ? ASTNodePtr{}
                                               : NodeFromJsonnet(param.expr);
            params.emplace_back(std::move(pname), std::move(pexpr));
        }
        return params;
    }

    [[nodiscard]] auto ParseField(::jsonnet::internal::ObjectField const& field,
                                  bool support_func = false)
        -> justlang::MapNode::entry_t;

  private:
    justlang::FileLocation input_file_;
    justlang::jsonnet::import_callback_t import_callback_;
    std::vector<justlang::FileLocation> import_chain_;
};

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Local>(
    ::jsonnet::internal::Local const* ast) -> ASTNodePtr {
    if (ast->binds.size() != 1) {
        throw ASTTranslateError{"Expected exactly one bind per local",
                                ast->location};
    }
    auto const& bind = ast->binds.front();

    auto loc = ToLocation(ast->location, import_chain_.back());
    auto name = ::jsonnet::internal::encode_utf8(bind.var->name);
    auto body = NodeFromJsonnet(bind.body);
    auto next = NodeFromJsonnet(ast->body);

    if (bind.functionSugar) {
        auto params = ToParams(bind.params);
        auto func = std::make_shared<justlang::FuncNode>(
            loc, std::move(params), std::move(body));
        return std::make_shared<justlang::LetNode>(
            loc, std::move(name), std::move(func), std::move(next));
    }
    return std::make_shared<justlang::LetNode>(
        loc, std::move(name), std::move(body), std::move(next));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Array>(
    ::jsonnet::internal::Array const* ast) -> ASTNodePtr {
    auto items = justlang::ListNode::items_t{};
    items.reserve(ast->elements.size());
    for (auto const& element : ast->elements) {
        items.emplace_back(NodeFromJsonnet(element.expr));
    }
    return std::make_shared<justlang::ListNode>(
        ToLocation(ast->location, import_chain_.back()), std::move(items));
}

auto ASTTranslator::ParseField(::jsonnet::internal::ObjectField const& field,
                               bool support_func)
    -> justlang::MapNode::entry_t {
    using ::jsonnet::internal::ObjectField;
    switch (field.kind) {
        case ObjectField::FIELD_EXPR:  // computed field    ['foo'+'bar']:
        case ObjectField::FIELD_ID:    // plain identifier  foobar:
        case ObjectField::FIELD_STR:   // string field      'foobar':
            break;
        case ObjectField::LOCAL:  // local definition
            throw ASTTranslateError{
                "Locals in maps are not allowed. Either specify them before "
                "the map or move them to the field's value.",
                field.idLocation};
        case ObjectField::ASSERT:  // assert definition
            throw ASTTranslateError{"Asserts are not supported.",
                                    field.idLocation};
        default:
            throw ASTTranslateError{"Unsupported field kind.",
                                    field.idLocation};
    }
    if (field.superSugar or                    // foo+:
        field.hide == ObjectField::HIDDEN or   // foo::
        field.hide == ObjectField::VISIBLE) {  // foo:::
        throw ASTTranslateError{"Unsupported field specifier.",
                                field.idLocation};
    }
    auto name = ASTNodePtr{};
    if (field.kind == ObjectField::FIELD_ID) {
        name = std::make_shared<justlang::StringNode>(
            ToLocation(field.idLocation, import_chain_.back()),
            ::jsonnet::internal::encode_utf8(field.id->name));
    }
    else {
        name = NodeFromJsonnet(field.expr1);
    }

    auto value = NodeFromJsonnet(field.expr2);

    if (field.methodSugar) {  // it's a function
        if (not support_func) {
            throw ASTTranslateError{"Field does not support functions.",
                                    field.idLocation};
        }
        value = std::make_shared<justlang::FuncNode>(
            ToLocation(field.idLocation, import_chain_.back()),
            ToParams(field.params),
            std::move(value));
    }
    return {std::move(name), std::move(value)};
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Object>(
    ::jsonnet::internal::Object const* ast) -> ASTNodePtr {
    auto fields = justlang::MapNode::fields_t{};
    fields.reserve(ast->fields.size());
    for (auto const& field : ast->fields) {
        fields.emplace_back(ParseField(field, /*support_func=*/true));
    }
    return std::make_shared<justlang::MapNode>(
        ToLocation(ast->location, import_chain_.back()), std::move(fields));
}

[[nodiscard]] auto ReadLiteralString(
    ::jsonnet::internal::LiteralString const* ast) noexcept
    -> std::optional<std::string> {
    using ::jsonnet::internal::encode_utf8;
    using ::jsonnet::internal::LiteralString;

    switch (ast->tokenKind) {
        case LiteralString::DOUBLE: {
            // Double-quoted strings are JSON encoded
            // -> unescape to get raw string
            return justlang::UnescapeJsonString(encode_utf8(ast->value));
        }
        case LiteralString::SINGLE: {
            // Single-quoted strings are JSON encoded, except for '"' and '\''
            // -> replace " by \"
            // -> replace \' by '
            // -> afterwards, unescape to get raw string
            auto oss = std::ostringstream{};
            auto str = encode_utf8(ast->value);
            auto start = str.cbegin();
            for (std::size_t i{}; i < str.size(); ++i) {
                auto pos = start + static_cast<int>(i);
                if (str.at(i) == '"') {
                    oss << "\\\"";
                }
                else if (i < str.size() - 1 and
                         std::string_view{pos, pos + 2} == "\\'") {
                    oss << "'";
                    ++i;
                }
                else {
                    oss << str.at(i);
                }
            }
            return justlang::UnescapeJsonString(oss.str());
        }
        case LiteralString::BLOCK:
        case LiteralString::VERBATIM_DOUBLE:
        case LiteralString::VERBATIM_SINGLE:
            // Block and verbatim strings are raw strings already
            return encode_utf8(ast->value);
    }
    return std::nullopt;
}

[[nodiscard]] auto TranslateString(
    ::jsonnet::internal::LiteralString const* ast,
    justlang::FileLocation const& origin,
    bool file_ref = false) -> ASTNodePtr {
    using ::jsonnet::internal::LiteralString;
    if (auto str = ReadLiteralString(ast)) {
        switch (ast->tokenKind) {
            case LiteralString::SINGLE:
            case LiteralString::DOUBLE:
            case LiteralString::BLOCK:
                return std::make_shared<justlang::StringNode>(
                    ToLocation(ast->location, origin), std::move(*str));
            case LiteralString::VERBATIM_SINGLE:
            case LiteralString::VERBATIM_DOUBLE:
                // Generate reference node
                return std::make_shared<justlang::RefNode>(
                    ToLocation(ast->location, origin),
                    justlang::DecodeRefString(*str, file_ref));
        }
    }
    throw ASTTranslateError{"Could not read literal string", ast->location};
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::LiteralNumber>(
    ::jsonnet::internal::LiteralNumber const* ast) -> ASTNodePtr {
    return std::make_shared<justlang::NumberNode>(
        ToLocation(ast->location, import_chain_.back()), ast->value);
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::LiteralBoolean>(
    ::jsonnet::internal::LiteralBoolean const* ast) -> ASTNodePtr {
    return std::make_shared<justlang::BoolNode>(
        ToLocation(ast->location, import_chain_.back()), ast->value);
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::LiteralString>(
    ::jsonnet::internal::LiteralString const* ast) -> ASTNodePtr {
    return TranslateString(ast, import_chain_.back());
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Var>(
    ::jsonnet::internal::Var const* ast) -> ASTNodePtr {
    return std::make_shared<justlang::CallNode>(
        ToLocation(ast->location, import_chain_.back()), ToString(ast));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Conditional>(
    ::jsonnet::internal::Conditional const* ast) -> ASTNodePtr {
    auto cond = NodeFromJsonnet(ast->cond);
    auto then_expr = NodeFromJsonnet(ast->branchTrue);
    auto else_expr = ASTNodePtr{};
    if (ast->branchFalse != nullptr) {
        else_expr = NodeFromJsonnet(ast->branchFalse);
    }
    return std::make_shared<justlang::IfNode>(
        ToLocation(ast->location, import_chain_.back()),
        std::move(cond),
        std::move(then_expr),
        std::move(else_expr));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::ArrayComprehension>(
    ::jsonnet::internal::ArrayComprehension const* ast) -> ASTNodePtr {
    if (ast->specs.size() != 1) {
        throw ASTTranslateError{
            "Expected exactly one spec per array comprehension", ast->location};
    }

    auto const& spec = ast->specs.front();
    if (spec.kind != jsonnet::internal::ComprehensionSpec::FOR) {
        throw ASTTranslateError{"Unsupported array comprehension spec.",
                                ast->location};
    }

    auto name = ::jsonnet::internal::encode_utf8(spec.var->name);
    auto range = NodeFromJsonnet(spec.expr);
    auto body = NodeFromJsonnet(ast->body);
    return std::make_shared<justlang::ForEachNode>(
        ToLocation(ast->location, import_chain_.back()),
        std::move(name),
        std::move(range),
        std::move(body));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::ObjectComprehension>(
    ::jsonnet::internal::ObjectComprehension const* ast) -> ASTNodePtr {
    if (ast->specs.size() != 1) {
        throw ASTTranslateError{
            "Expected exactly one spec per object comprehension",
            ast->location};
    }

    auto const& spec = ast->specs.front();
    if (spec.kind != jsonnet::internal::ComprehensionSpec::FOR) {
        throw ASTTranslateError{"Unsupported object comprehension spec.",
                                ast->location};
    }

    // Let's not reinvent the wheel for object comprehensions, just translate:
    //     {<key_expr>:<value_expr> for <var> in <range_expr>}
    // to union of array comprehension with singleton map body:
    //     union([{<key_expr>:<value_expr>} for <var> in <range_expr>])
    auto loc = ToLocation(ast->location, import_chain_.back());
    auto name = ::jsonnet::internal::encode_utf8(spec.var->name);
    auto range = NodeFromJsonnet(spec.expr);
    auto fields = justlang::MapNode::fields_t{};
    fields.reserve(ast->fields.size());
    for (auto const& field : ast->fields) {
        fields.emplace_back(ParseField(field));
    }
    auto make_map = std::make_shared<justlang::MapNode>(loc, std::move(fields));
    auto foreach = std::make_shared<justlang::ForEachNode>(
        ToLocation(ast->location, import_chain_.back()),
        std::move(name),
        std::move(range),
        std::move(make_map));
    static auto const kUnionBuiltin =
        std::make_shared<justlang::BuiltinNode>("union");
    return std::make_shared<justlang::CallNode>(
        loc,
        kUnionBuiltin,
        justlang::CallNode::params_t{{"maps", std::move(foreach)}});
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Function>(
    ::jsonnet::internal::Function const* ast) -> ASTNodePtr {
    auto params = justlang::FuncNode::params_t{};
    params.reserve(ast->params.size());
    for (auto const& param : ast->params) {
        params.emplace_back(
            ::jsonnet::internal::encode_utf8(param.id->name),
            param.expr == nullptr ? ASTNodePtr{} : NodeFromJsonnet(param.expr));
    }
    auto body = NodeFromJsonnet(ast->body);
    return std::make_shared<justlang::FuncNode>(
        ToLocation(ast->location, import_chain_.back()),
        std::move(params),
        std::move(body));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Unary>(
    ::jsonnet::internal::Unary const* ast) -> ASTNodePtr {
    using UnaryOp = ::jsonnet::internal::UnaryOp;
    using UnaryType = justlang::UnaryOperationNode::Type;

    UnaryType unary_type{};
    auto value_type = justlang::ValueType::Any;
    switch (ast->op) {
        case UnaryOp::UOP_NOT:
            unary_type = UnaryType::Not;
            value_type = justlang::ValueType::Bool;
            break;
        case UnaryOp::UOP_MINUS:
            unary_type = UnaryType::Minus;
            value_type = justlang::ValueType::Number;
            break;

        case UnaryOp::UOP_BITWISE_NOT:
        case UnaryOp::UOP_PLUS:
        default:
            throw ASTTranslateError{
                "Unsupported unary operation " +
                    ::jsonnet::internal::uop_string(ast->op),
                ast->location};
    }
    return std::make_shared<justlang::UnaryOperationNode>(
        ToLocation(ast->location, import_chain_.back()),
        unary_type,
        NodeFromJsonnet(ast->expr),
        value_type);
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Binary>(
    ::jsonnet::internal::Binary const* ast) -> ASTNodePtr {
    using BinaryOp = jsonnet::internal::BinaryOp;
    auto loc = ToLocation(ast->location, import_chain_.back());
    auto lhs = NodeFromJsonnet(ast->left);
    auto rhs = NodeFromJsonnet(ast->right);

    bool negate{};
    justlang::BinaryOperationNode::Type node_op{};
    justlang::ValueType eval_type = justlang::ValueType::Any;
    switch (ast->op) {
        case BinaryOp::BOP_MANIFEST_EQUAL:
            node_op = justlang::BinaryOperationNode::Type::Equal;
            eval_type = justlang::ValueType::Bool;
            break;
        case BinaryOp::BOP_MANIFEST_UNEQUAL:
            negate = true;
            node_op = justlang::BinaryOperationNode::Type::Equal;
            eval_type = justlang::ValueType::Bool;
            break;
        case BinaryOp::BOP_AND:
            node_op = justlang::BinaryOperationNode::Type::And;
            eval_type = justlang::ValueType::Bool;
            break;
        case BinaryOp::BOP_OR:
            node_op = justlang::BinaryOperationNode::Type::Or;
            eval_type = justlang::ValueType::Bool;
            break;
        case BinaryOp::BOP_PLUS:
            node_op = justlang::BinaryOperationNode::Type::Plus;
            // '+' may be called on numbers, strings, lists or maps.
            // At this point the exact result type is unknown,
            // it will be deduced at the inlining step.
            eval_type = justlang::ValueType::Any;
            break;
        case BinaryOp::BOP_MINUS:
            node_op = justlang::BinaryOperationNode::Type::Plus;
            rhs = std::make_shared<justlang::UnaryOperationNode>(
                loc, justlang::UnaryOperationNode::Type::Minus, std::move(rhs));
            eval_type = justlang::ValueType::Number;
            break;
        case BinaryOp::BOP_MULT:
            node_op = justlang::BinaryOperationNode::Type::Multiplication;
            eval_type = justlang::ValueType::Number;
            break;

        case BinaryOp::BOP_DIV:
        case BinaryOp::BOP_PERCENT:
        case BinaryOp::BOP_SHIFT_L:
        case BinaryOp::BOP_SHIFT_R:
        case BinaryOp::BOP_GREATER:
        case BinaryOp::BOP_GREATER_EQ:
        case BinaryOp::BOP_LESS:
        case BinaryOp::BOP_LESS_EQ:
        case BinaryOp::BOP_IN:
        case BinaryOp::BOP_BITWISE_AND:
        case BinaryOp::BOP_BITWISE_XOR:
        case BinaryOp::BOP_BITWISE_OR:
        default:
            throw ASTTranslateError{
                "Unsupported binary operator " +
                    ::jsonnet::internal::bop_string(ast->op),
                ast->location};
    }

    ASTNodePtr node = std::make_shared<justlang::BinaryOperationNode>(
        loc, node_op, lhs, rhs, eval_type);

    if (negate) {
        node = std::make_shared<justlang::UnaryOperationNode>(
            loc, justlang::UnaryOperationNode::Type::Not, std::move(node));
    }

    return node;
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Index>(
    ::jsonnet::internal::Index const* ast) -> ASTNodePtr {
    if (ast->isSlice) {
        throw ASTTranslateError{"Slice is unsupported for index",
                                ast->location};
    }

    auto key = ASTNodePtr{};
    if (ast->id == nullptr) {
        key = NodeFromJsonnet(ast->index);
    }
    else {
        auto name = ::jsonnet::internal::encode_utf8(ast->id->name);
        key = std::make_shared<justlang::StringNode>(
            ToLocation(ast->location, import_chain_.back()), std::move(name));
    }

    return std::make_shared<justlang::LookupNode>(
        ToLocation(ast->location, import_chain_.back()),
        std::move(key),
        NodeFromJsonnet(ast->target));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::LiteralNull>(
    ::jsonnet::internal::LiteralNull const* ast) -> ASTNodePtr {
    return std::make_shared<justlang::NullNode>(
        ToLocation(ast->location, import_chain_.back()));
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Import>(
    ::jsonnet::internal::Import const* ast) -> ASTNodePtr {
    auto file = std::string{};
    auto repo = std::optional<std::string>{};

    // parse as file-reference
    auto node = ASTNodePtr{};
    try {
        node =
            TranslateString(ast->file, import_chain_.back(), /*file_ref=*/true);
    } catch (...) {
        throw ASTTranslateError{
            "Failed to parse external file-reference @'<repo>//<file_path>'.",
            ast->location};
    }
    if (auto const* str =
            justlang::ASTNode::Cast<justlang::StringNode const*>(node.get())) {
        // plain path
        file = str->GetValue();
    }
    else if (auto const* ref =
                 justlang::ASTNode::Cast<justlang::RefNode const*>(
                     node.get())) {
#ifdef SUPPORT_IMPORT_VIA_EXT_REF
        // encoded file-reference: @'<repo>//<file>'
        auto const& data = ref->GetRefData();
        if (data.type != justlang::RefType::Ext) {
            throw ASTTranslateError{
                "Missing repository for import from external file-reference "
                "@'<repo>//<file_path>'.",
                ast->location};
        }
        file = data.module;
        repo = data.repo;
#else
        (void)ref;
        throw ASTTranslateError{"Only imports from plain path are suppored.",
                                ast->location};
#endif
    }

    if (auto data =
            import_callback_(input_file_, file, repo ? &(*repo) : nullptr)) {

        // detect cycle
        auto import_it =
            std::find(import_chain_.begin(), import_chain_.end(), data->origin);
        if (import_it != import_chain_.end()) {
            // compute cycle graph and throw exception
            auto oss = std::ostringstream{};
            oss << ",-> " << import_it->ToString() << "\n";
            while (std::distance(++import_it, import_chain_.end()) > 1) {
                oss << "|   " << import_it->ToString() << "\n";
            }
            if (import_it == import_chain_.end()) {
                oss << "`---'\n";
            }
            else {
                oss << "`-- " << import_it->ToString() << "\n";
            }
            throw ASTTranslateError{
                "Detected cycle in import chain.\n" + oss.str(), ast->location};
        }

        auto translator =
            ASTTranslator{data->origin, import_callback_, import_chain_};
        return std::make_shared<justlang::ForeignNode>(
            ToLocation(ast->location, import_chain_.back()),
            translator.NodeFromJsonnet(data->ast));
    }

    throw ASTTranslateError{"Failed to import file '" + file + "'.",
                            ast->location};
}

template <>
auto ASTTranslator::MakeNode<::jsonnet::internal::Apply>(
    ::jsonnet::internal::Apply const* ast) -> ASTNodePtr {
    if (ast->target == nullptr) {
        throw ASTTranslateError{"Missing apply target.", ast->location};
    }

    auto params = justlang::CallNode::params_t{};
    params.reserve(ast->args.size());
    for (auto const& param : ast->args) {
        auto pname = std::optional<std::string>{};
        if (param.id != nullptr) {
            pname = jsonnet::internal::encode_utf8(param.id->name);
        }
        auto pexpr = NodeFromJsonnet(param.expr);
        params.emplace_back(std::move(pname), std::move(pexpr));
    }

    if (auto const* var =
            dynamic_cast<jsonnet::internal::Var const*>(ast->target)) {
        // call function from "global scope"
        return std::make_shared<justlang::CallNode>(
            ToLocation(ast->location, import_chain_.back()),
            ToString(var),
            std::move(params));
    }

    if (auto const* lookup =
            dynamic_cast<jsonnet::internal::Index const*>(ast->target)) {
        // call function attached to object
        auto target = MakeNode(lookup);
        if (justlang::ASTNode::Cast<justlang::LookupNode const*>(
                target.get()) == nullptr) {
            throw ASTTranslateError{
                "Expected lookup node from index expression.",
                lookup->location};
        }
        return std::make_shared<justlang::CallNode>(
            ToLocation(ast->location, import_chain_.back()),
            target,
            std::move(params));
    }

    if (auto const* call =
            dynamic_cast<jsonnet::internal::Apply const*>(ast->target)) {
        // call function from return value of call
        auto target = NodeFromJsonnet(call);
        if (justlang::ASTNode::Cast<justlang::CallNode const*>(target.get()) ==
            nullptr) {
            throw ASTTranslateError{"Expected call node from apply expression.",
                                    call->location};
        }
        return std::make_shared<justlang::CallNode>(
            ToLocation(ast->location, import_chain_.back()),
            target,
            std::move(params));
    }
    throw ASTTranslateError{
        "Unsupported call target type " +
            jsonnet::internal::ASTTypeToString(ast->target->type),
        ast->target->location};
}

auto ASTTranslator::NodeFromJsonnet(::jsonnet::internal::AST const* expr)
    -> ASTNodePtr {
    // NOLINTNEXTLINE(google-build-using-namespace)
    using namespace ::jsonnet::internal;

    if (expr == nullptr) {
        throw ASTTranslateError{"Cannot translate nullptr"};
    }

    try {
        // Jsonnet nodes
        if (auto const* ast = dynamic_cast<Object const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Array const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Local const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<LiteralBoolean const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<LiteralString const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<LiteralNumber const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Var const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Conditional const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<ArrayComprehension const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<ObjectComprehension const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Function const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Unary const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Binary const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Index const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<LiteralNull const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Import const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Apply const*>(expr)) {
            return MakeNode(ast);
        }
        if (auto const* ast = dynamic_cast<Parens const*>(expr)) {
            return NodeFromJsonnet(ast->expr);
        }
    } catch (std::exception const& ex) {
        throw ASTTranslateError{"While translating '" +
                                ASTTypeToString(expr->type) + "':\n" +
                                ex.what()};
    }

    throw ASTTranslateError{
        "Unsupported AST type " + ASTTypeToString(expr->type), expr->location};
}

}  // namespace

ASTTranslateError::ASTTranslateError(std::string const& error)
    : runtime_error{error} {}

ASTTranslateError::ASTTranslateError(
    std::string const& error,
    ::jsonnet::internal::LocationRange const& loc)
    : runtime_error{"In " + GetLocationString(loc) + ":\n  Error: " + error} {}

auto justlang::jsonnet::TranslateToJust(::jsonnet::internal::AST const* expr,
                                        justlang::FileLocation input_file,
                                        import_callback_t import_callback)
    -> ASTNodePtr {
    auto translator =
        ASTTranslator{std::move(input_file), std::move(import_callback), {}};
    return translator.NodeFromJsonnet(expr);
}
