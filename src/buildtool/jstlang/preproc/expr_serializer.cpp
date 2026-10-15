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

#include "src/buildtool/jstlang/preproc/expr_serializer.hpp"

#include <string>
#include <unordered_map>

#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"

namespace {

using jstlang::ASTNode;
using jstlang::VerbatimType;

constexpr auto kFlat = VerbatimType::Flat;
constexpr auto kFull = VerbatimType::Full;

/// \brief Fields of targets (per target type) that are not evaluated, with
/// the kind of their verbatim serialization; all other fields are serialized
/// as expressions.
auto const kUnevaluatedTargetFields =
    std::unordered_map<std::string,
                       std::unordered_map<std::string, VerbatimType>>{
        // default
        {"", {{"type", kFull}, {"arguments_config", kFull}}},
        // export target
        {"export",
         {{"type", kFull},
          {"arguments_config", kFull},
          {"fixed_config", kFull},
          {"doc", kFull},
          {"config_doc", kFull}}},
        // install target
        {"install",
         {{"type", kFull}, {"arguments_config", kFull}, {"files", kFlat}}}};

/// \brief Literal map, with all field names being string literals.
[[nodiscard]] auto LiteralMap(ASTNode const& node) -> jstlang::MapNode const& {
    auto const* map = ASTNode::Cast<jstlang::MapNode const*>(&node);
    if (map == nullptr) {
        throw jstlang::ExprSerializerError{"AST node is not a literal map."};
    }
    for (auto const& [key, _] : map->GetFields()) {
        if (ASTNode::Cast<jstlang::StringNode const*>(key.get()) == nullptr) {
            throw jstlang::ExprSerializerError{
                "Expected literal string as field name for literal map."};
        }
    }
    return *map;
}

[[nodiscard]] auto FieldName(jstlang::ASTNodePtr const& key)
    -> std::string const& {
    return ASTNode::Cast<jstlang::StringNode const*>(key.get())->GetValue();
}

[[nodiscard]] auto SerializeTarget(ASTNode const& node) -> nlohmann::json {
    auto const& target = LiteralMap(node);

    auto target_type = std::string{};
    for (auto const& [key, value] : target.GetFields()) {
        if (FieldName(key) == "type") {
            if (auto const* str =
                    ASTNode::Cast<jstlang::StringNode const*>(value.get())) {
                target_type = str->GetValue();
            }
            else if (ASTNode::Cast<jstlang::RefNode const*>(value.get()) ==
                     nullptr) {
                throw jstlang::ExprSerializerError{
                    "Expected literal string or reference for target type."};
            }
            // else: not a built-in rule, that's fine
        }
    }
    auto const& uneval_fields = kUnevaluatedTargetFields.contains(target_type)
                                    ? kUnevaluatedTargetFields.at(target_type)
                                    : kUnevaluatedTargetFields.at("");

    // the target is a plain JSON object, its fields are serialized verbatim
    // (if not evaluated) or as expressions; the latest field wins
    auto json = nlohmann::json::object();
    for (auto const& [key, value] : target.GetFields()) {
        if (ASTNode::Cast<jstlang::FuncNode const*>(value.get()) != nullptr) {
            continue;  // functions are not serialized
        }
        auto const& name = FieldName(key);
        auto const it = uneval_fields.find(name);
        auto const vtype =
            it != uneval_fields.end() ? it->second : VerbatimType::None;
        json[name] = jstlang::ASTToJsonVisitor{}.ToJson(*value, vtype);
    }
    return json;
}

[[nodiscard]] auto SerializeTargets(ASTNode const& node) -> nlohmann::json {
    // the targets are a plain JSON object, the latest target wins
    auto json = nlohmann::json::object();
    for (auto const& [key, value] : LiteralMap(node).GetFields()) {
        json[FieldName(key)] = SerializeTarget(*value);
    }
    return json;
}

}  // namespace

auto jstlang::ExprSerializer::Serialize(ASTNode const& ast) const
    -> nlohmann::json {
    switch (type_) {
        case FileType::Plain:
            return ASTToJsonVisitor{}.ToJson(ast);
        case FileType::Targets:
            return SerializeTargets(ast);
        case FileType::Rules:
        case FileType::Expressions:
            break;
    }
    throw ExprSerializerError{"Not yet implemented file type."};
}
