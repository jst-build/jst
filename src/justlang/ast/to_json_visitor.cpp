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

#include "justlang/ast/to_json_visitor.hpp"

#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/ast.hpp"
#include "justlang/ref.hpp"

namespace {

auto const kEmptyMap = nlohmann::json({{"type", "empty_map"}});

// Create singleton_map expression, returns nullopt if field value is a FuncNode
[[nodiscard]] auto CreateSingletonMap(justlang::ASTToJsonVisitor const* visitor,
                                      justlang::MapNode::entry_t const& field)
    -> std::optional<nlohmann::json> {
    auto const& [key, value] = field;
    if (justlang::ASTNode::Cast<justlang::FuncNode const*>(value.get()) !=
        nullptr) {
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

namespace justlang {

auto ASTToJsonVisitor::ToJson(ASTNode const& node,
                              VerbatimType next_vtype) const -> nlohmann::json {
    if (vtype_ == next_vtype or vtype_ == VerbatimType::Full) {
        return std::visit(*this, node.ToVariant());
    }
    return ASTToJsonVisitor{next_vtype, json_only_}.ToJson(node, next_vtype);
}

auto ASTToJsonVisitor::operator()(VerbatimNode const* node) const
    -> nlohmann::json {
    if (json_only_) {
        throw std::runtime_error{"not allowed"};
    }
    return ToJson(*node->GetExpr(), node->GetType());
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
            // error
            return {};
        }
        if (ASTNode::Cast<FuncNode const*>(value.get()) != nullptr) {
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
        return node->GetRefData().target;
    }

    auto root = nlohmann::json::array();
    if (ref_data.type == RefType::Ext) {
        root.push_back("@");
        root.push_back(ref_data.repo);
    }
    else if (ref_data.type == RefType::Rel) {
        root.push_back("./");
    }
    root.push_back(ref_data.module);
    root.push_back(ref_data.target);
    return root;
}

}  // namespace justlang
