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

#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ref.hpp"

namespace {

auto const kEmptyMap = nlohmann::json({{"type", "empty_map"}});

// Create singleton_map expression, returns nullopt if field value is a function
[[nodiscard]] auto CreateSingletonMap(jstlang::ASTToJsonVisitor const* visitor,
                                      jstlang::MapNode::entry_t const& field)
    -> std::optional<nlohmann::json> {
    auto const& [key, value] = field;
    if (value->IsFunction()) {
        // skip serialization of func nodes
        return std::nullopt;
    }
    auto jmap = nlohmann::json::object();
    jmap["type"] = "singleton_map";
    jmap["key"] = visitor->ToJson(*key);
    jmap["value"] = visitor->ToJson(*value);
    return jmap;
}

}  // namespace

namespace jstlang {

auto ASTToJsonVisitor::ToJson(ASTNode const& node,
                              VerbatimType next_vtype) const -> nlohmann::json {
    if (vtype_ == next_vtype or vtype_ == VerbatimType::Full) {
        return std::visit(*this, node.ToVariant());
    }
    return ASTToJsonVisitor{next_vtype, json_only_}.ToJson(node, next_vtype);
}

auto ASTToJsonVisitor::operator()(BuiltinCallNode const* node) const
    -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    struct Expression {
        std::string type;
        // keys of the built-in's arguments, by parameter position
        std::vector<std::string> keys;
    };
    static auto const kExpressions =
        std::unordered_map<std::string, Expression>{
            {"get", {"lookup", {"key", "map", "default"}}},
            {"join", {"join", {"$1", "separator"}}},
            {"flatten", {"++", {"$1"}}},
            // type depends on the parameter "disjoint", which is not serialized
            // type and msg depend on the parameter "disjoint", which is
            // not serialized
            {"union", {"map_union", {"$1", "", "msg"}}},
            {"keys", {"keys", {"$1"}}},
            {"fail", {"fail", {"msg"}}},
            {"json_encode", {"json_encode", {"$1"}}},
            {"at", {"[]", {"index", "list", "default"}}},
            {"all", {"and", {"$1"}}},
            {"any", {"or", {"$1"}}},
            {"sum", {"+", {"$1"}}},
            {"prod", {"*", {"$1"}}},
            {"eq", {"==", {"$1", "$2"}}},
            {"not", {"not", {"$1"}}},
            {"zip_map", {"zip_map", {"range_key", "range_val"}}},
            {"nub_right", {"nub_right", {"$1"}}},
            {"nub_left", {"nub_left", {"$1"}}},
            {"range", {"range", {"$1"}}},
            {"reverse", {"reverse", {"$1"}}},
            {"length", {"length", {"$1"}}},
            {"basename", {"basename", {"$1"}}},
            {"join_cmd", {"join_cmd", {"$1"}}},
            {"change_ending", {"change_ending", {"$1", "ending"}}},
            {"escape_chars",
             {"escape_chars", {"$1", "chars", "escape_prefix"}}},
            {"enumerate", {"enumerate", {"$1"}}},
            {"to_subdir", {"to_subdir", {"$1", "subdir", "flat", "msg"}}},
            {"from_subdir", {"from_subdir", {"$1", "subdir"}}},
            {"values", {"values", {"$1"}}},
            {"assert_non_empty", {"assert_non_empty", {"$1", "msg"}}},
            {"context", {"context", {"$1", "msg"}}},
            // rule-only functions
            {"field", {"FIELD", {"name"}}},
            {"dep_artifacts", {"DEP_ARTIFACTS", {"dep", "transition"}}},
            {"dep_runfiles", {"DEP_RUNFILES", {"dep", "transition"}}},
            {"dep_provides",
             {"DEP_PROVIDES", {"dep", "provider", "transition", "default"}}},
            {"make_blob", {"BLOB", {"data"}}},
            {"make_symlink", {"SYMLINK", {"data"}}},
            {"make_tree", {"TREE", {"$1"}}},
            // type depends on the parameter "disjoint", which is not
            // serialized
            {"tree_overlay", {"TREE_OVERLAY", {"$1", ""}}},
            {"action",
             {"ACTION",
              {"inputs",
               "cmd",
               "outs",
               "out_dirs",
               "env",
               "cwd",
               "label",
               "may_fail",
               "fail_message",
               "no_cache",
               "timeout scaling",
               "execution properties"}}},
            {"result", {"RESULT", {"artifacts", "runfiles", "provides"}}},
            {"value_node", {"VALUE_NODE", {"$1"}}},
            {"abstract_node",
             {"ABSTRACT_NODE",
              {"node_type", "string_fields", "target_fields"}}},
        };
    static auto const kEmptyMapNode =
        std::make_shared<MapNode>(Location{}, MapNode::fields_t{});

    auto const& name = node->GetName();
    auto const expr_it = kExpressions.find(name);
    if (expr_it == kExpressions.end()) {
        throw std::runtime_error{"Unknown built-in call " + name};
    }
    auto const& [type, keys] = expr_it->second;
    auto const& args = node->GetArgs();

    auto root = nlohmann::json::object();
    root["type"] = type;
    bool const disjoint = (name == "union" or name == "tree_overlay") and
                          args.size() > 1 and args[1] != nullptr and
                          args[1]->TruthValue().value_or(false);
    if (disjoint) {
        root["type"] =
            name == "union" ? "disjoint_map_union" : "DISJOINT_TREE_OVERLAY";
    }
    for (std::size_t pos = 0; pos < args.size() and pos < keys.size(); ++pos) {
        auto const& arg = args[pos];
        if (arg == nullptr or arg->IsFunction() or keys[pos].empty()) {
            // skip unspecified arguments, func nodes, unserialized arguments
            continue;
        }
        if (name == "union" and keys[pos] == "msg" and not disjoint) {
            continue;  // only a disjoint union has a message
        }
        if (name == "get" and keys[pos] == "map" and
            arg->EvalType() == ValueType::Any) {
            // robustness: fallback to empty map for types evaluated at runtime
            auto const map =
                IfNode{arg->GetLocation(), arg, arg, kEmptyMapNode};
            root[keys[pos]] = ToJson(map);
            continue;
        }
        root[keys[pos]] = ToJson(*arg);
    }
    return root;
}

auto ASTToJsonVisitor::ExternalValueToJson(ExternalValueNode const& node) const
    -> nlohmann::json {
    if (not json_only_) {
        throw std::runtime_error{TypeToString(node.EvalType()) +
                                 " values cannot be serialized."};
    }
    // as for the expression language, values that are not JSON are null
    return node.GetValue()->ToJson();
}

auto ASTToJsonVisitor::operator()(ListNode const* node) const
    -> nlohmann::json {
    auto root = nlohmann::json::array();
    root.get_ptr<nlohmann::json::array_t*>()->reserve(node->GetItems().size());
    for (auto const& item : node->GetItems()) {
        root.push_back(ToJson(*item));
    }
    return root;
}

auto ASTToJsonVisitor::operator()(MapNode const* node) const -> nlohmann::json {
    if (not json_only_ and vtype_ == VerbatimType::None) {
        auto num_fields = node->GetFields().size();

        if (num_fields == 0) {
            return kEmptyMap;
        }

        if (num_fields == 1) {
            if (auto jmap =
                    CreateSingletonMap(this, node->GetFields().front())) {
                return *jmap;
            }
            return kEmptyMap;
        }

        auto jlist = nlohmann::json::array();
        for (auto const& field : node->GetFields()) {
            if (auto jmap = CreateSingletonMap(this, field)) {
                jlist.push_back(std::move(*jmap));
            }
        }

        auto root = nlohmann::json::object();
        root["type"] = "map_union";
        root["$1"] = std::move(jlist);
        return root;
    }

    // serialize to literal map
    auto root = nlohmann::json::object();
    for (auto const& [key, value] : node->GetFields()) {
        auto const* field_name = ASTNode::Cast<StringNode const*>(key.get());
        if (field_name == nullptr) {
            throw std::runtime_error{
                "Field names of literal maps must be strings."};
        }
        if (value->IsFunction()) {
            // skip serialization of func nodes
            continue;
        }
        root[field_name->GetValue()] = ToJson(*value);
    }
    return root;
}

auto ASTToJsonVisitor::operator()(BoolNode const* node) const
    -> nlohmann::json {
    return node->GetValue();
}

auto ASTToJsonVisitor::operator()(StringNode const* node) const
    -> nlohmann::json {
    return node->GetValue();
}

auto ASTToJsonVisitor::operator()(NumberNode const* node) const
    -> nlohmann::json {
    return node->GetValue();
}

auto ASTToJsonVisitor::operator()(NullNode const* /*node*/) const
    -> nlohmann::json {
    return nullptr;
}

auto ASTToJsonVisitor::operator()(VarNode const* node) const -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    auto root = nlohmann::json::object();
    root["type"] = "var";
    root["name"] = node->GetName();
    if (node->GetDefault() != nullptr) {
        root["default"] = ToJson(*node->GetDefault());
    }
    return root;
}

auto ASTToJsonVisitor::operator()(IfNode const* node) const -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    auto root = nlohmann::json::object();
    root["type"] = "if";
    root["cond"] = ToJson(*node->GetCondition());
    if (node->GetThenBranch() != nullptr) {
        root["then"] = ToJson(*node->GetThenBranch());
    }
    if (node->GetElseBranch() != nullptr) {
        root["else"] = ToJson(*node->GetElseBranch());
    }
    return root;
}

auto ASTToJsonVisitor::operator()(ForEachNode const* node) const
    -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    auto root = nlohmann::json::object();
    root["type"] = "foreach";
    root["var"] = node->GetVariable();
    root["range"] = ToJson(*node->GetRange());
    root["body"] = ToJson(*node->GetBody());
    return root;
}

auto ASTToJsonVisitor::operator()(ZipWithNode const* node) const
    -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    auto root = nlohmann::json::object();
    root["type"] = "zip_with";
    root["var_1"] = node->GetVariable1();
    root["var_2"] = node->GetVariable2();
    root["range_1"] = ToJson(*node->GetRange1());
    root["range_2"] = ToJson(*node->GetRange2());
    root["body"] = ToJson(*node->GetBody());
    return root;
}

auto ASTToJsonVisitor::operator()(FoldLeftNode const* node) const
    -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    auto root = nlohmann::json::object();
    root["type"] = "foldl";
    root["var"] = node->GetIterVar();
    root["accum_var"] = node->GetAccuVar();
    root["start"] = ToJson(*node->GetInit());
    root["range"] = ToJson(*node->GetRange());
    root["body"] = ToJson(*node->GetBody());
    return root;
}

auto ASTToJsonVisitor::operator()(RefNode const* node) const -> nlohmann::json {
    auto const& ref_data = node->GetRefData();
    if (ref_data.type == RefType::Local) {
        return node->GetRefData().target.value_or("");
    }

    auto root = nlohmann::json::array();
    if (ref_data.type == RefType::Ext) {
        root.push_back("@");
        root.push_back(ref_data.repo.value_or(""));
    }
    else if (ref_data.type == RefType::Rel) {
        root.push_back("./");
    }
    root.push_back(ref_data.module);
    root.push_back(ref_data.target.value_or(""));
    return root;
}

}  // namespace jstlang
