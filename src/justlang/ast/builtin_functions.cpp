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

#include "justlang/ast/builtin_functions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/ast.hpp"
#include "justlang/ast/to_json_visitor.hpp"

namespace {

auto const kNullNode =
    std::make_shared<justlang::NullNode>(justlang::Location{});

auto const kEmptyMap =
    std::make_shared<justlang::MapNode>(justlang::Location{},
                                        justlang::MapNode::fields_t{});

auto const kEmptyString =
    std::make_shared<justlang::StringNode>(justlang::Location{}, std::string{});

auto const kDotPath =
    std::make_shared<justlang::StringNode>(justlang::Location{}, ".");

auto const kFalseBool =
    std::make_shared<justlang::BoolNode>(justlang::Location{}, false);

class BuiltInError final : public std::runtime_error {
  public:
    explicit BuiltInError(justlang::Location const& loc, std::string message)
        : std::runtime_error(MakeMessage(loc, std::move(message))) {}

  private:
    static auto MakeMessage(justlang::Location const& loc,
                            std::string message) -> std::string;
};

using NamedParams = std::unordered_map<std::string, justlang::ASTNodePtr>;

struct ExpectedParameter final {
    std::string Name;
    // accepted types, empty means all types are accepted
    std::unordered_set<justlang::ValueType> Types;
    bool Mandatory = false;
};

template <std::size_t N>
[[nodiscard]] auto MakeNamedParameters(
    justlang::CallNode::params_t const& source_args,
    std::array<ExpectedParameter, N> const& expected_args) -> NamedParams;

template <typename T = justlang::ASTNode>
[[nodiscard]] auto RetrieveAs(NamedParams const& params,
                              ExpectedParameter const& to_retrieve)
    -> std::shared_ptr<T>;

[[nodiscard]] auto NodeToJson(justlang::ASTNodePtr const& node,
                              bool json_only) -> std::optional<nlohmann::json>;

[[nodiscard]] auto ProduceSameJSON(justlang::ASTNodePtr const& lhs,
                                   justlang::ASTNodePtr const& rhs,
                                   bool json_only) -> std::optional<bool>;

[[nodiscard]] auto CheckEqual(justlang::ASTNodePtr const& lhs_node,
                              justlang::ASTNodePtr const& rhs_node)
    -> std::optional<bool>;

[[nodiscard]] auto MakeBuiltinObjEntry(std::string const& name)
    -> justlang::MapNode::entry_t;

[[nodiscard]] auto MakeBuiltinObj() -> justlang::ASTNodePtr;

[[nodiscard]] auto NodeToNumber(justlang::ASTNodePtr const& node)
    -> std::optional<double>;

[[nodiscard]] auto ShellQuote(std::string arg) -> std::string;

[[nodiscard]] auto ToNormalPath(std::filesystem::path const& path) noexcept
    -> std::filesystem::path;

}  // namespace

namespace justlang {

auto BuiltIn::env(Location const& loc,
                  CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"name", {ValueType::String}, true},
        ExpectedParameter{"default", {}, false},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("env: ") + e.what());
    }

    const auto name_node = RetrieveAs<StringNode>(named_params, kExpected[0]);
    if (name_node == nullptr) {
        throw BuiltInError(loc, "env: Missing mandatory parameter \"name\"");
    }
    auto default_node = RetrieveAs(named_params, kExpected[1]);
    return std::make_shared<VarNode>(
        loc, name_node->GetValue(), std::move(default_node));
}

auto BuiltIn::get(Location const& loc,
                  CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"key", {ValueType::Any, ValueType::String}, true},
        ExpectedParameter{"map", {ValueType::Any, ValueType::Map}, true},
        ExpectedParameter{"default", {}, false},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("get: ") + e.what());
    }

    ASTNodePtr key_node = RetrieveAs(named_params, kExpected[0]);
    if (key_node == nullptr) {
        throw BuiltInError(loc, "get: Missing mandatory parameter \"key\"");
    }
    ASTNodePtr map_node = RetrieveAs(named_params, kExpected[1]);
    if (map_node == nullptr) {
        throw BuiltInError(loc, "get: Missing mandatory parameter \"map\"");
    }
    ASTNodePtr default_node = RetrieveAs(named_params, kExpected[2]);
    if (not default_node) {
        default_node = kNullNode;
    }

    // attempt static evaluation
    auto const* map_obj = ASTNode::Cast<MapNode const*>(map_node.get());
    auto const* key_str = ASTNode::Cast<StringNode const*>(key_node.get());
    if (map_obj != nullptr and key_str != nullptr) {
        auto const& lookup_name = key_str->GetValue();
        bool has_computed_field_names{};
        // latest wins, so we have to lookup in reverse order
        auto const& fields = map_obj->GetFields();
        auto field_it = fields.rbegin();
        while (field_it != fields.rend()) {
            auto const& field = *field_it++;
            if (auto const* name_str =
                    ASTNode::Cast<StringNode const*>(field.first.get())) {
                if (name_str->GetValue() == lookup_name) {
                    if (ASTNode::Cast<NullNode const*>(field.second.get()) !=
                        nullptr) {
                        return default_node;
                    }
                    return field.second;
                }
            }
            else {
                // we cannot match computed fields, defer to runtime evaluation
                has_computed_field_names = true;
                break;
            }
        }
        if (not has_computed_field_names) {
            // not found: fallback to default
            return default_node;
        }
    }

    if (map_node->EvalType() == ValueType::Any) {
        // robustness: fallback to empty map for types evaluated at runtime
        map_node = std::make_shared<IfNode>(loc, map_node, map_node, kEmptyMap);
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "lookup")},
        {std::make_shared<StringNode>(loc, "key"), key_node},
        {std::make_shared<StringNode>(loc, "map"), map_node}};

    if (default_node != kNullNode) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "default"),
                            default_node);
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Any);
}

auto BuiltIn::join(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"items", {ValueType::Any, ValueType::List}, true},
        ExpectedParameter{"sep", {ValueType::Any, ValueType::String}, false},
    };
    static auto const kItemTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::String};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("join: ") + e.what());
    }

    ASTNodePtr items_node = RetrieveAs(named_params, kExpected[0]);
    if (items_node == nullptr) {
        throw BuiltInError(loc, "join: Missing mandatory parameter \"items\"");
    }
    ASTNodePtr const separator = RetrieveAs(named_params, kExpected[1]);

    // attempt static evaluation
    auto const* items_list = ASTNode::Cast<ListNode const*>(items_node.get());
    auto const* sep_str = ASTNode::Cast<StringNode const*>(separator.get());
    if (items_list != nullptr and sep_str == separator.get()) {
        bool contains_non_string_items{};
        for (auto const& item_node : items_list->GetItems()) {
            if (not kItemTypes.contains(item_node->EvalType())) {
                throw BuiltInError(loc,
                                   "join: List may not contain items of type " +
                                       TypeToString(item_node->EvalType()));
            }
            if (ASTNode::Cast<StringNode const*>(item_node.get()) == nullptr) {
                contains_non_string_items = true;
                break;
            }
        }

        if (not contains_non_string_items) {
            auto sep = sep_str == nullptr ? std::string{} : sep_str->GetValue();
            auto out = std::string{};
            for (auto const& item_node : items_list->GetItems()) {
                auto const* item =
                    ASTNode::Cast<StringNode const*>(item_node.get());
                if (not out.empty()) {
                    out += sep;
                }
                out += item->GetValue();
            }
            return std::make_shared<StringNode>(justlang::Location{},
                                                std::move(out));
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "join")},
        {std::make_shared<StringNode>(loc, "$1"), items_node},
    };

    if (separator) {
        fields.emplace_back(std::make_pair(
            std::make_shared<StringNode>(loc, "separator"), separator));
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::String);
}

auto BuiltIn::flatten(Location const& loc,
                      CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"lists", {ValueType::Any, ValueType::List}, true},
    };
    static auto const kItemTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::List};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("flatten: ") + e.what());
    }

    ASTNodePtr lists_node = RetrieveAs(named_params, kExpected[0]);
    if (lists_node == nullptr) {
        throw BuiltInError(loc,
                           "flatten: Missing mandatory parameter \"lists\"");
    }

    // attempt static evaluation
    auto const* items_list = ASTNode::Cast<ListNode const*>(lists_node.get());
    if (items_list != nullptr) {
        auto output_list = ListNode::items_t{};
        bool contains_non_list_items{};
        for (auto const& item_node : items_list->GetItems()) {
            if (auto const* item =
                    ASTNode::Cast<ListNode const*>(item_node.get())) {
                auto const& list = item->GetItems();
                output_list.insert(output_list.end(), list.begin(), list.end());
            }
            else if (not kItemTypes.contains(item_node->EvalType())) {
                throw BuiltInError(
                    loc,
                    "flatten: List may not contain items of type " +
                        TypeToString(item_node->EvalType()));
            }
            else {
                contains_non_list_items = true;
                break;
            }
        }
        if (not contains_non_list_items) {
            return std::make_shared<ListNode>(justlang::Location{},
                                              std::move(output_list));
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "++")},
        {std::make_shared<StringNode>(loc, "$1"), lists_node},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::List);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto BuiltIn::map_union(Location const& loc,
                        CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"maps", {ValueType::Any, ValueType::List}, true},
        ExpectedParameter{"disjoint", {}, false},
    };
    static auto const kItemTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::Map};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("union: ") + e.what());
    }

    ASTNodePtr maps_node = RetrieveAs(named_params, kExpected[0]);
    if (maps_node == nullptr) {
        throw BuiltInError(loc, "union: Missing mandatory parameter \"maps\"");
    }
    ASTNodePtr disjoint_node = RetrieveAs(named_params, kExpected[1]);
    if (not disjoint_node) {
        disjoint_node = kNullNode;
    }
    auto disjoint_value = disjoint_node->TruthValue();
    if (not disjoint_value) {
        throw BuiltInError(loc,
                           "union: Parameter \"disjoint\" must be a literal.");
    }

    // attempt static evaluation
    auto const* items_list = ASTNode::Cast<ListNode const*>(maps_node.get());
    if (items_list != nullptr and disjoint_value) {
        bool contains_non_static_maps{};

        auto max_num_fields = std::size_t{};
        for (auto const& item_node : items_list->GetItems()) {
            if (auto const* map =
                    ASTNode::Cast<MapNode const*>(item_node.get())) {
                max_num_fields += map->GetFields().size();
            }
            else if (not kItemTypes.contains(item_node->EvalType())) {
                throw BuiltInError(
                    loc,
                    "union: List may not contain items of type " +
                        TypeToString(item_node->EvalType()));
            }
            else {
                contains_non_static_maps = true;
                break;
            }
        }

        if (not contains_non_static_maps) {
            // Do worst case allocation
            auto output_fields = MapNode::fields_t{};
            output_fields.reserve(max_num_fields);

            // Auxiliary map for tracking field positions in vector
            auto field_pos = std::unordered_map<std::string, std::size_t>{};

            // Compute unified output_fields
            // TODO(oreiche): speed up via binary tree merge
            for (auto const& item_node : items_list->GetItems()) {
                auto const* map =
                    ASTNode::Cast<MapNode const*>(item_node.get());
                auto const& fields = map->GetFields();
                for (auto const& [name_node, value_node] : fields) {
                    if (auto const* name =
                            ASTNode::Cast<StringNode const*>(name_node.get())) {
                        auto found_it = field_pos.find(name->GetValue());
                        if (found_it == field_pos.end()) {
                            // add new field to vector
                            output_fields.emplace_back(name_node, value_node);
                            field_pos[name->GetValue()] =
                                output_fields.size() - 1;
                        }
                        else {
                            auto& existing_field =
                                output_fields.at(found_it->second);
                            if (*disjoint_value) {
                                auto equal = CheckEqual(value_node,
                                                        existing_field.second);
                                if (not equal) {
                                    // undecidable, needs runtime evaluation
                                    contains_non_static_maps = true;
                                    break;
                                }
                                if (not *equal) {
                                    throw BuiltInError(
                                        loc,
                                        "disjoint union: Got "
                                        "conflicting field name \"" +
                                            name->GetValue() + "\"");
                                }
                            }
                            else {
                                // update existing field
                                existing_field.second = value_node;
                            }
                        }
                    }
                    else {
                        // not static, due to computed field names
                        contains_non_static_maps = true;
                        break;
                    }
                }

                if (contains_non_static_maps) {
                    break;
                }
            }

            if (not contains_non_static_maps) {
                // success, return unified map
                return std::make_shared<MapNode>(justlang::Location{},
                                                 std::move(output_fields));
            }
        }
    }

    // static evaluation failed, fallback to creating runtime expressions

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(
             loc, *disjoint_value ? "disjoint_map_union" : "map_union")},
        {std::make_shared<StringNode>(loc, "$1"), maps_node},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::List);
}

auto BuiltIn::keys(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"map", {ValueType::Any, ValueType::Map}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("keys: ") + e.what());
    }

    ASTNodePtr map_node = RetrieveAs(named_params, kExpected[0]);
    if (map_node == nullptr) {
        throw BuiltInError(loc, "keys: Missing mandatory parameter \"map\"");
    }
    if (auto const* map_obj = ASTNode::Cast<MapNode const*>(map_node.get())) {
        // literal map -> generate list of keys
        auto keys = ListNode::items_t{};
        keys.reserve(map_obj->GetFields().size());
        for (auto const& [key, _] : map_obj->GetFields()) {
            keys.emplace_back(key);
        }
        return std::make_shared<ListNode>(loc, std::move(keys));
    }

    MapNode::fields_t verbatim_map_fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "keys")},
        {std::make_shared<StringNode>(loc, "$1"), map_node},
    };
    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(verbatim_map_fields)),
        VerbatimType::Flat,
        ValueType::List);
}

auto BuiltIn::fail(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"msg", {}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("fail: ") + e.what());
    }

    ASTNodePtr msg_node = RetrieveAs(named_params, kExpected[0]);
    if (msg_node == nullptr) {
        throw BuiltInError(loc, "fail: Missing mandatory parameter \"msg\"");
    }

    MapNode::fields_t verbatim_map_fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "fail")},
        {std::make_shared<StringNode>(loc, "msg"), std::move(msg_node)},
    };
    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(verbatim_map_fields)),
        VerbatimType::Flat,
        ValueType::Any);
}

auto BuiltIn::json_encode(Location const& loc,
                          CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"data", {}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("json_encode: ") + e.what());
    }

    ASTNodePtr data_node = RetrieveAs(named_params, kExpected[0]);
    if (data_node == nullptr) {
        throw BuiltInError(loc,
                           "json_encode: Missing mandatory parameter \"data\"");
    }

    // attempt partial evaluation
    if (auto json = NodeToJson(data_node, /*json_only=*/true)) {
        // json_only only succeeds if data contains no runtime variable
        return std::make_shared<StringNode>(loc, json->dump());
    }
    // fallback to generating a runtime expression

    MapNode::fields_t verbatim_map_fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "json_encode")},
        {std::make_shared<StringNode>(loc, "$1"), data_node},
    };
    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(verbatim_map_fields)),
        VerbatimType::Flat,
        ValueType::String);
}

auto BuiltIn::file(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"path", {ValueType::String}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("file: ") + e.what());
    }

    auto const path_node = RetrieveAs<StringNode>(named_params, kExpected[0]);
    if (path_node == nullptr) {
        throw BuiltInError(loc, "file: Missing mandatory parameter \"path\"");
    }
    ListNode::items_t items = {std::make_shared<StringNode>(loc, "FILE"),
                               std::make_shared<NullNode>(loc),
                               path_node};
    return std::make_shared<ListNode>(loc, std::move(items));
}

auto BuiltIn::symlink(Location const& loc,
                      CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"path", {ValueType::String}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("symlink: ") + e.what());
    }

    auto const path_node = RetrieveAs<StringNode>(named_params, kExpected[0]);
    if (path_node == nullptr) {
        throw BuiltInError(loc, "file: Missing mandatory parameter \"path\"");
    }
    ListNode::items_t items = {std::make_shared<StringNode>(loc, "SYMLINK"),
                               std::make_shared<NullNode>(loc),
                               path_node};
    return std::make_shared<ListNode>(loc, std::move(items));
}

auto BuiltIn::tree(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"path", {ValueType::String}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("tree: ") + e.what());
    }

    auto const path_node = RetrieveAs<StringNode>(named_params, kExpected[0]);
    if (path_node == nullptr) {
        throw BuiltInError(loc, "tree: Missing mandatory parameter \"path\"");
    }

    ListNode::items_t items = {std::make_shared<StringNode>(loc, "TREE"),
                               std::make_shared<NullNode>(loc),
                               path_node};
    return std::make_shared<ListNode>(loc, std::move(items));
}

auto BuiltIn::glob(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"pattern", {ValueType::String}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("glob: ") + e.what());
    }

    auto const pattern_node =
        RetrieveAs<StringNode>(named_params, kExpected[0]);
    if (pattern_node == nullptr) {
        throw BuiltInError(loc,
                           "glob: Missing mandatory parameter \"pattern\"");
    }

    ListNode::items_t items = {std::make_shared<StringNode>(loc, "GLOB"),
                               std::make_shared<NullNode>(loc),
                               pattern_node};
    return std::make_shared<ListNode>(loc, std::move(items));
}

auto BuiltIn::at(Location const& loc,
                 CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{
            "index",
            {ValueType::Any, ValueType::String, ValueType::Number},
            true},
        ExpectedParameter{"list", {ValueType::Any, ValueType::List}, true},
        ExpectedParameter{"default", {}, false},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("at: ") + e.what());
    }

    ASTNodePtr index_node = RetrieveAs(named_params, kExpected[0]);
    if (index_node == nullptr) {
        throw BuiltInError(loc, "at: Missing mandatory parameter \"index\"");
    }
    ASTNodePtr list_node = RetrieveAs(named_params, kExpected[1]);
    if (list_node == nullptr) {
        throw BuiltInError(loc, "at: Missing mandatory parameter \"list\"");
    }
    ASTNodePtr default_node = RetrieveAs(named_params, kExpected[2]);
    if (not default_node) {
        default_node = kNullNode;
    }

    // attempt partial evaluation
    auto const* list_obj = ASTNode::Cast<ListNode const*>(list_node.get());
    auto const* index_str = ASTNode::Cast<StringNode const*>(index_node.get());
    auto const* index_num = ASTNode::Cast<NumberNode const*>(index_node.get());
    if (list_obj != nullptr and
        (index_str != nullptr or index_num != nullptr)) {

        auto list_index = std::int64_t{0};
        if (index_num != nullptr) {
            // round to the nearest integer:
            list_index = std::lround(index_num->GetValue());
        }
        else if (index_str != nullptr) {
            // convert index from string to a number:
            try {
                list_index = std::stol(index_str->GetValue());
            } catch (...) {
                list_index = 0;
            }
        }

        // negative numbers access elements from the end of the list:
        if (list_index < 0) {
            list_index +=
                static_cast<std::int64_t>(list_obj->GetItems().size());
        }

        // access the element:
        if (list_index < 0 or static_cast<std::int64_t>(
                                  list_obj->GetItems().size()) <= list_index) {
            return default_node;
        }
        return list_obj->GetItems().at(static_cast<std::size_t>(list_index));
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "[]")},
        {std::make_shared<StringNode>(loc, "index"), index_node},
        {std::make_shared<StringNode>(loc, "list"), list_node}};

    if (default_node != kNullNode) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "default"),
                            default_node);
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Any);
}

auto BuiltIn::all(Location const& loc,
                  CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"args", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("all: ") + e.what());
    }

    ASTNodePtr args_node = RetrieveAs(named_params, kExpected[0]);
    if (args_node == nullptr) {
        throw BuiltInError(loc, "all: Missing mandatory parameter \"args\"");
    }

    // attempt partial evaluation
    auto const* args_list = ASTNode::Cast<ListNode const*>(args_node.get());
    if (args_list != nullptr) {
        bool contains_non_static_arg{};
        for (auto const& arg : args_list->GetItems()) {
            auto truth = arg->TruthValue();
            if (not truth) {
                contains_non_static_arg = true;
            }
            else if (not *truth) {
                return std::make_shared<BoolNode>(loc, false);
            }
        }

        if (not contains_non_static_arg) {
            return std::make_shared<BoolNode>(loc, true);
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "and")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(args_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Bool);
}

auto BuiltIn::any(Location const& loc,
                  CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"args", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("any: ") + e.what());
    }

    ASTNodePtr args_node = RetrieveAs(named_params, kExpected[0]);
    if (args_node == nullptr) {
        throw BuiltInError(loc, "any: Missing mandatory parameter \"args\"");
    }

    // attempt partial evaluation
    auto const* args_list = ASTNode::Cast<ListNode const*>(args_node.get());
    if (args_list != nullptr) {
        bool contains_non_static_arg{};
        for (auto const& arg : args_list->GetItems()) {
            auto truth = arg->TruthValue();
            if (not truth) {
                contains_non_static_arg = true;
            }
            else if (*truth) {
                return std::make_shared<BoolNode>(loc, true);
            }
        }

        if (not contains_non_static_arg) {
            return std::make_shared<BoolNode>(loc, false);
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "or")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(args_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Bool);
}

auto BuiltIn::sum(Location const& loc,
                  CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"numbers", {ValueType::Any, ValueType::List}, true},
    };
    static auto const kItemTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::Number};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("sum: ") + e.what());
    }

    ASTNodePtr numbers_node = RetrieveAs(named_params, kExpected[0]);
    if (numbers_node == nullptr) {
        throw BuiltInError(loc, "sum: Missing mandatory parameter \"numbers\"");
    }

    // attempt partial evaluation
    auto const* numbers_list =
        ASTNode::Cast<ListNode const*>(numbers_node.get());
    if (numbers_list != nullptr) {
        double sum{};
        bool contains_non_static_number{};
        for (auto const& number : numbers_list->GetItems()) {
            if (auto const* num_val =
                    ASTNode::Cast<NumberNode const*>(number.get())) {
                sum += num_val->GetValue();
            }
            else if (not kItemTypes.contains(number->EvalType())) {
                throw BuiltInError(loc,
                                   "sum: List may not contain items of type " +
                                       TypeToString(number->EvalType()));
            }
            else {
                contains_non_static_number = true;
                break;
            }
        }

        if (not contains_non_static_number) {
            return std::make_shared<NumberNode>(loc, sum);
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "+")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(numbers_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Number);
}

auto BuiltIn::prod(Location const& loc,
                   CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"numbers", {ValueType::Any, ValueType::List}, true},
    };
    static auto const kItemTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::Number};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("prod: ") + e.what());
    }

    ASTNodePtr numbers_node = RetrieveAs(named_params, kExpected[0]);
    if (numbers_node == nullptr) {
        throw BuiltInError(loc,
                           "prod: Missing mandatory parameter \"numbers\"");
    }

    // attempt partial evaluation
    auto const* numbers_list =
        ASTNode::Cast<ListNode const*>(numbers_node.get());
    if (numbers_list != nullptr) {
        double prod{1.0};
        bool contains_non_static_number{};
        for (auto const& number : numbers_list->GetItems()) {
            if (auto const* num_val =
                    ASTNode::Cast<NumberNode const*>(number.get())) {
                prod *= num_val->GetValue();
            }
            else if (not kItemTypes.contains(number->EvalType())) {
                throw BuiltInError(loc,
                                   "prod: List may not contain items of type " +
                                       TypeToString(number->EvalType()));
            }
            else {
                contains_non_static_number = true;
                break;
            }
        }

        if (not contains_non_static_number) {
            return std::make_shared<NumberNode>(loc, prod);
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "*")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(numbers_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Number);
}

auto BuiltIn::eq(Location const& loc,
                 CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"lhs", {}, true},
        ExpectedParameter{"rhs", {}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("eq: ") + e.what());
    }

    ASTNodePtr lhs_node = RetrieveAs(named_params, kExpected[0]);
    if (lhs_node == nullptr) {
        throw BuiltInError(loc, "eq: Missing mandatory parameter \"lhs\"");
    }

    ASTNodePtr rhs_node = RetrieveAs(named_params, kExpected[1]);
    if (rhs_node == nullptr) {
        throw BuiltInError(loc, "eq: Missing mandatory parameter \"rhs\"");
    }

    // attempt partial evaluation
    auto equal_value = CheckEqual(lhs_node, rhs_node);
    if (equal_value) {
        return std::make_shared<BoolNode>(loc, *equal_value);
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "==")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(lhs_node)},
        {std::make_shared<StringNode>(loc, "$2"), std::move(rhs_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Bool);
}

auto BuiltIn::negate(Location const& loc,
                     CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"expr", {}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("not: ") + e.what());
    }

    ASTNodePtr expr_node = RetrieveAs(named_params, kExpected[0]);
    if (expr_node == nullptr) {
        throw BuiltInError(loc, "not: Missing mandatory parameter \"lhs\"");
    }

    // attempt partial evaluation
    if (auto truth = expr_node->TruthValue()) {
        return std::make_shared<BoolNode>(loc, not *truth);
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "not")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(expr_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Bool);
}

auto BuiltIn::foreach (Location const& loc,
                       CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"func", {}, true},
        ExpectedParameter{"range", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("foreach: ") + e.what());
    }

    auto func_node = RetrieveAs<FuncNode>(named_params, kExpected[0]);
    if (func_node == nullptr) {
        throw BuiltInError(loc,
                           "foreach: Mandatory parameter \"func\" is missing "
                           "or not a function");
    }

    ASTNodePtr range_node = RetrieveAs(named_params, kExpected[1]);
    if (range_node == nullptr) {
        throw BuiltInError(loc,
                           "foreach: Missing mandatory parameter \"range\"");
    }

    if (func_node->GetParams().size() != 1) {
        throw BuiltInError(
            loc,
            "foreach: Parameter \"func\" must accept exactly one parameter.");
    }

    auto const& iter_var = func_node->GetParams().at(0).first;
    auto const& body = func_node->GetBody();
    return {std::make_shared<ForEachNode>(
                loc, iter_var, std::move(range_node), body),
            /*needs_inlining=*/true};
}

auto BuiltIn::foldl(Location const& loc,
                    CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"func", {}, true},
        ExpectedParameter{"init", {}, true},
        ExpectedParameter{"range", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("foldl: ") + e.what());
    }

    auto func_node = RetrieveAs<FuncNode>(named_params, kExpected[0]);
    if (func_node == nullptr) {
        throw BuiltInError(
            loc,
            "foldl: Mandatory parameter \"func\" is missing or not a function");
    }

    ASTNodePtr init_node = RetrieveAs(named_params, kExpected[1]);
    if (init_node == nullptr) {
        throw BuiltInError(loc, "foldl: Missing mandatory parameter \"init\"");
    }

    ASTNodePtr range_node = RetrieveAs(named_params, kExpected[2]);
    if (range_node == nullptr) {
        throw BuiltInError(loc, "foldl: Missing mandatory parameter \"range\"");
    }

    if (func_node->GetParams().size() != 2) {
        throw BuiltInError(
            loc,
            "foldl: Parameter \"func\" must accept exactly two parameters.");
    }

    auto const& iter_var = func_node->GetParams().at(0).first;
    auto const& accu_var = func_node->GetParams().at(1).first;
    auto const& body = func_node->GetBody();
    return {std::make_shared<FoldLeftNode>(loc,
                                           iter_var,
                                           accu_var,
                                           std::move(init_node),
                                           std::move(range_node),
                                           body),
            /*needs_inlining=*/true};
}

auto BuiltIn::nub_right(Location const& loc,
                        CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"list", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("nub_right: ") + e.what());
    }

    ASTNodePtr list_node = RetrieveAs(named_params, kExpected[0]);
    if (list_node == nullptr) {
        throw BuiltInError(loc,
                           "nub_right: Missing mandatory parameter \"list\"");
    }

    // attempt partial evaluation
    if (auto const* list = ASTNode::Cast<ListNode const*>(list_node.get())) {
        auto const& items = list->GetItems();
        if (items.size() <= 1) {
            return std::make_shared<ListNode>(loc, items);
        }

        auto out_items = ListNode::items_t{};
        auto seen = std::unordered_set<std::string>{};
        out_items.reserve(items.size());
        seen.reserve(items.size());
        auto serializer =
            ASTToJsonVisitor{VerbatimType::Full, /*json_only=*/true};
        bool contains_non_serializable_items{};
        auto item_it = items.rbegin();
        while (item_it != items.rend()) {
            auto const& item_node = *item_it++;
            try {
                auto json_str = serializer.ToJson(*item_node).dump();
                if (seen.emplace(std::move(json_str)).second) {
                    out_items.emplace_back(item_node);
                }
            } catch (...) {
                // fallback to generating a runtime expression
                contains_non_serializable_items = true;
                break;
            }
        }
        if (not contains_non_serializable_items) {
            std::reverse(out_items.begin(), out_items.end());
            return std::make_shared<ListNode>(loc, std::move(out_items));
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "nub_right")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(list_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::List);
}

auto BuiltIn::nub_left(Location const& loc,
                       CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"list", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("nub_left: ") + e.what());
    }

    ASTNodePtr list_node = RetrieveAs(named_params, kExpected[0]);
    if (list_node == nullptr) {
        throw BuiltInError(loc,
                           "nub_left: Missing mandatory parameter \"list\"");
    }

    // attempt partial evaluation
    if (auto const* list = ASTNode::Cast<ListNode const*>(list_node.get())) {
        auto const& items = list->GetItems();
        if (items.size() <= 1) {
            return std::make_shared<ListNode>(loc, items);
        }

        auto out_items = ListNode::items_t{};
        auto seen = std::unordered_set<std::string>{};
        out_items.reserve(items.size());
        seen.reserve(items.size());
        auto serializer =
            ASTToJsonVisitor{VerbatimType::Full, /*json_only=*/true};
        bool contains_non_serializable_items{};
        for (auto const& item_node : items) {
            try {
                auto json_str = serializer.ToJson(*item_node).dump();
                if (seen.emplace(std::move(json_str)).second) {
                    out_items.emplace_back(item_node);
                }
            } catch (...) {
                // fallback to generating a runtime expression
                contains_non_serializable_items = true;
                break;
            }
        }
        if (not contains_non_serializable_items) {
            return std::make_shared<ListNode>(loc, std::move(out_items));
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "nub_left")},
        {std::make_shared<StringNode>(loc, "$1"), std::move(list_node)},
    };

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::List);
}

auto BuiltIn::range(Location const& loc,
                    CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"size", {}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("range: ") + e.what());
    }

    ASTNodePtr size_node = RetrieveAs(named_params, kExpected[0]);
    if (size_node == nullptr) {
        throw BuiltInError(loc, "range: Missing mandatory parameter \"size\"");
    }

    // attempt partial evaluation
    if (auto size_number = NodeToNumber(size_node)) {
        // round to the nearest integer:
        auto list_range = static_cast<int>(std::round(*size_number));
        // negative numbers are ignored
        if (list_range < 0) {
            list_range = 0;
        }

        auto items = ListNode::items_t{};
        items.reserve(list_range);

        for (int i{}; i < list_range; ++i) {
            items.emplace_back(
                std::make_shared<StringNode>(loc, std::to_string(i)));
        }
        return std::make_shared<ListNode>(loc, std::move(items));
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "range")},
        {std::make_shared<StringNode>(loc, "$1"), size_node}};

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::List);
}

auto BuiltIn::reverse(Location const& loc,
                      CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"list", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("reverse: ") + e.what());
    }

    ASTNodePtr list_node = RetrieveAs(named_params, kExpected[0]);
    if (list_node == nullptr) {
        throw BuiltInError(loc,
                           "reverse: Missing mandatory parameter \"list\"");
    }

    // attempt partial evaluation
    if (auto const* list = ASTNode::Cast<ListNode const*>(list_node.get())) {
        auto const& items = list->GetItems();
        if (items.size() <= 1) {
            return list_node;
        }
        return std::make_shared<ListNode>(
            loc, ListNode::items_t{items.rbegin(), items.rend()});
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "reverse")},
        {std::make_shared<StringNode>(loc, "$1"), list_node}};

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::List);
}

auto BuiltIn::length(Location const& loc,
                     CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"list", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("length: ") + e.what());
    }

    ASTNodePtr list_node = RetrieveAs(named_params, kExpected[0]);
    if (list_node == nullptr) {
        throw BuiltInError(loc, "length: Missing mandatory parameter \"list\"");
    }

    // attempt partial evaluation
    if (auto const* list = ASTNode::Cast<ListNode const*>(list_node.get())) {
        return std::make_shared<NumberNode>(
            loc, static_cast<double>(list->GetItems().size()));
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "length")},
        {std::make_shared<StringNode>(loc, "$1"), list_node}};

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Number);
}

auto BuiltIn::basename(Location const& loc,
                       CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"path", {ValueType::Any, ValueType::String}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("basename: ") + e.what());
    }

    ASTNodePtr path_node = RetrieveAs(named_params, kExpected[0]);
    if (path_node == nullptr) {
        throw BuiltInError(loc,
                           "basename: Missing mandatory parameter \"path\"");
    }

    // attempt partial evaluation
    if (auto const* str = ASTNode::Cast<StringNode const*>(path_node.get())) {
        return std::make_shared<StringNode>(
            loc, std::filesystem::path{str->GetValue()}.filename().string());
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "basename")},
        {std::make_shared<StringNode>(loc, "$1"), path_node}};

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::String);
}

auto BuiltIn::join_cmd(Location const& loc,
                       CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"args", {ValueType::Any, ValueType::List}, true},
    };
    static auto const kArgTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::String};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("join_cmd: ") + e.what());
    }

    ASTNodePtr args_node = RetrieveAs(named_params, kExpected[0]);
    if (args_node == nullptr) {
        throw BuiltInError(loc,
                           "join_cmd: Missing mandatory parameter \"args\"");
    }

    // attempt partial evaluation
    if (auto const* args = ASTNode::Cast<ListNode const*>(args_node.get())) {
        bool contains_non_string_items{};
        for (auto const& arg_node : args->GetItems()) {
            if (not kArgTypes.contains(arg_node->EvalType())) {
                throw BuiltInError(
                    loc,
                    "join_cmd: List may not contain args of type " +
                        TypeToString(arg_node->EvalType()));
            }
            if (ASTNode::Cast<StringNode const*>(arg_node.get()) == nullptr) {
                contains_non_string_items = true;
                break;
            }
        }

        if (not contains_non_string_items) {
            auto sep = std::string{" "};
            auto out = std::string{};
            for (auto const& arg_node : args->GetItems()) {
                auto const* item =
                    ASTNode::Cast<StringNode const*>(arg_node.get());
                if (not out.empty()) {
                    out += sep;
                }
                out += ShellQuote(item->GetValue());
            }
            return std::make_shared<StringNode>(justlang::Location{},
                                                std::move(out));
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "join_cmd")},
        {std::make_shared<StringNode>(loc, "$1"), args_node}};

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::String);
}

auto BuiltIn::change_ending(Location const& loc,
                            CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"path", {ValueType::Any, ValueType::String}, true},
        ExpectedParameter{"ending", {ValueType::Any, ValueType::String}, false},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("change_ending: ") + e.what());
    }

    ASTNodePtr path_node = RetrieveAs(named_params, kExpected[0]);
    if (path_node == nullptr) {
        throw BuiltInError(
            loc, "change_ending: Missing mandatory parameter \"path\"");
    }

    ASTNodePtr ending_node = RetrieveAs(named_params, kExpected[1]);
    if (ending_node == nullptr) {
        ending_node = kEmptyString;
    }

    auto const* path = ASTNode::Cast<StringNode const*>(path_node.get());
    auto const* ending = ASTNode::Cast<StringNode const*>(ending_node.get());

    // attempt partial evaluation
    if (path != nullptr and ending != nullptr) {
        auto const fspath = std::filesystem::path{path->GetValue()};
        return std::make_shared<StringNode>(
            loc,
            (fspath.parent_path() / fspath.stem()).string() +
                ending->GetValue());
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "change_ending")},
        {std::make_shared<StringNode>(loc, "$1"), path_node}};

    if (ending_node != kEmptyString) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "ending"),
                            ending_node);
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::String);
}

auto BuiltIn::escape_chars(Location const& loc,
                           CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"str", {ValueType::Any, ValueType::String}, true},
        ExpectedParameter{"chars", {ValueType::Any, ValueType::String}, false},
        ExpectedParameter{"prefix", {ValueType::Any, ValueType::String}, false},
    };
    static const auto kPrefixString =
        std::make_shared<StringNode>(Location{}, std::string{"\\"});

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("escape_chars: ") + e.what());
    }

    ASTNodePtr str_node = RetrieveAs(named_params, kExpected[0]);
    if (str_node == nullptr) {
        throw BuiltInError(loc,
                           "escape_chars: Missing mandatory parameter \"str\"");
    }

    ASTNodePtr chars_node = RetrieveAs(named_params, kExpected[1]);
    if (chars_node == nullptr) {
        chars_node = kEmptyString;
    }

    ASTNodePtr prefix_node = RetrieveAs(named_params, kExpected[2]);
    if (prefix_node == nullptr) {
        prefix_node = kPrefixString;
    }

    auto const* str = ASTNode::Cast<StringNode const*>(str_node.get());
    auto const* chars = ASTNode::Cast<StringNode const*>(chars_node.get());
    auto const* prefix = ASTNode::Cast<StringNode const*>(prefix_node.get());

    // attempt partial evaluation
    if (str != nullptr and chars != nullptr and prefix != nullptr) {
        auto const& str_val = str->GetValue();
        auto const& chars_val = chars->GetValue();
        std::unordered_set<char> chars_set(chars_val.begin(), chars_val.end());
        std::ostringstream oss{};
        std::for_each(str_val.begin(), str_val.end(), [&](auto strc) {
            oss << (chars_set.contains(strc) ? prefix->GetValue() : "") << strc;
        });
        return std::make_shared<StringNode>(loc, oss.str());
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "escape_chars")},
        {std::make_shared<StringNode>(loc, "$1"), str_node}};

    if (chars_node != kEmptyString) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "chars"),
                            chars_node);
    }
    if (prefix_node != kPrefixString) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "escape_prefix"),
                            prefix_node);
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::String);
}

auto BuiltIn::enumerate(Location const& loc,
                        CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"items", {ValueType::Any, ValueType::List}, true},
    };
    static auto const kArgTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::String};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("enumerate: ") + e.what());
    }

    ASTNodePtr items_node = RetrieveAs(named_params, kExpected[0]);
    if (items_node == nullptr) {
        throw BuiltInError(loc,
                           "enumerate: Missing mandatory parameter \"items\"");
    }

    // attempt partial evaluation
    if (auto const* list = ASTNode::Cast<ListNode const*>(items_node.get())) {
        auto const& items = list->GetItems();
        auto fields = MapNode::fields_t{};
        fields.reserve(items.size());
        int pos{};
        for (auto const& item : items) {
            static constexpr auto kLeadingZeros = 10;
            std::ostringstream oss{};
            oss << std::setw(kLeadingZeros) << std::setfill('0') << pos;
            fields.emplace_back(std::make_shared<StringNode>(loc, oss.str()),
                                item);
            ++pos;
        }
        return std::make_shared<MapNode>(loc, std::move(fields));
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "enumerate")},
        {std::make_shared<StringNode>(loc, "$1"), items_node}};

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Map);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto BuiltIn::to_subdir(Location const& loc,
                        CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"map", {ValueType::Any, ValueType::Map}, true},
        ExpectedParameter{"subdir", {ValueType::Any, ValueType::String}, false},
        ExpectedParameter{"flat", {}, false},
        ExpectedParameter{"msg", {ValueType::String}, false},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("to_subdir: ") + e.what());
    }

    ASTNodePtr map_node = RetrieveAs(named_params, kExpected[0]);
    if (map_node == nullptr) {
        throw BuiltInError(loc,
                           "to_subdir: Missing mandatory parameter \"map\"");
    }

    ASTNodePtr subdir_node = RetrieveAs(named_params, kExpected[1]);
    if (subdir_node == nullptr) {
        subdir_node = kDotPath;
    }

    ASTNodePtr flat_node = RetrieveAs(named_params, kExpected[2]);
    if (flat_node == nullptr) {
        flat_node = kFalseBool;
    }

    ASTNodePtr msg_node = RetrieveAs(named_params, kExpected[3]);

    // attempt partial evaluation
    if (auto const* map = ASTNode::Cast<MapNode const*>(map_node.get())) {
        auto const& fields = map->GetFields();
        if (fields.empty()) {
            return std::make_shared<MapNode>(loc, fields);
        }
        auto const* subdir_path =
            ASTNode::Cast<StringNode const*>(subdir_node.get());
        auto flat_value = flat_node->TruthValue();
        if (subdir_path != nullptr and flat_value) {
            auto const& subdir = std::filesystem::path{subdir_path->GetValue()};
            auto out_fields = MapNode::fields_t{};
            out_fields.reserve(fields.size());
            auto seen = std::unordered_map<std::string, ASTNodePtr>{};
            seen.reserve(fields.size());
            bool needs_runtime_eval{};

            for (auto const& [key_node, val_node] : fields) {
                auto const* key_str =
                    ASTNode::Cast<StringNode const*>(key_node.get());
                if (key_str == nullptr) {
                    // computed fields can only be checked at runtime
                    needs_runtime_eval = true;
                    break;
                }
                auto path = std::filesystem::path{key_str->GetValue()};
                auto new_path = subdir / (*flat_value ? path.filename() : path);
                auto new_key = ToNormalPath(new_path).string();
                auto inserted = seen.emplace(new_key, val_node);
                if (not inserted.second) {
                    auto equal_value =
                        CheckEqual(inserted.first->second, val_node);
                    if (not equal_value) {
                        // might evaluate same, needs runtime evaluation
                        needs_runtime_eval = true;
                        break;
                    }
                    if (not *equal_value) {
                        std::ostringstream oss{};
                        if (auto const* msg_str =
                                ASTNode::Cast<StringNode const*>(
                                    msg_node.get())) {
                            if (not msg_str->GetValue().empty()) {
                                oss << msg_str->GetValue() << "\n";
                            }
                        }
                        oss << "Reason: staging to subdir " << subdir.string()
                            << " conflicts on new path " << new_key << "\n";
                        throw BuiltInError{loc, oss.str()};
                    }
                }
                else {
                    out_fields.emplace_back(
                        std::make_shared<StringNode>(loc, new_key), val_node);
                }
            }

            if (not needs_runtime_eval) {
                return std::make_shared<MapNode>(loc, std::move(out_fields));
            }
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "to_subdir")},
        {std::make_shared<StringNode>(loc, "$1"), map_node}};

    if (subdir_node != kDotPath) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "subdir"),
                            std::move(subdir_node));
    }

    if (flat_node != kFalseBool) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "flat"),
                            std::move(flat_node));
    }

    if (msg_node) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "msg"),
                            std::move(msg_node));
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Map);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto BuiltIn::from_subdir(Location const& loc,
                          CallNode::params_t const& args) -> Result {
    static const std::array kExpected = {
        ExpectedParameter{"map", {ValueType::Any, ValueType::Map}, true},
        ExpectedParameter{"subdir", {ValueType::Any, ValueType::String}, false},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("from_subdir: ") + e.what());
    }

    ASTNodePtr map_node = RetrieveAs(named_params, kExpected[0]);
    if (map_node == nullptr) {
        throw BuiltInError(loc,
                           "from_subdir: Missing mandatory parameter \"map\"");
    }

    ASTNodePtr subdir_node = RetrieveAs(named_params, kExpected[1]);
    if (subdir_node == nullptr) {
        subdir_node = kDotPath;
    }

    // attempt partial evaluation
    if (auto const* map = ASTNode::Cast<MapNode const*>(map_node.get())) {
        auto const& fields = map->GetFields();
        if (fields.empty()) {
            return std::make_shared<MapNode>(loc, fields);
        }
        if (auto const* subdir_path =
                ASTNode::Cast<StringNode const*>(subdir_node.get())) {
            auto const& subdir = ToNormalPath(subdir_path->GetValue());
            auto out_fields = MapNode::fields_t{};
            out_fields.reserve(fields.size());
            auto seen = std::unordered_map<std::string, ASTNodePtr>{};
            seen.reserve(fields.size());
            bool needs_runtime_eval{};

            for (auto const& [key_node, val_node] : fields) {
                auto const* key_str =
                    ASTNode::Cast<StringNode const*>(key_node.get());
                if (key_str == nullptr) {
                    // computed fields can only be checked at runtime
                    needs_runtime_eval = true;
                    break;
                }
                auto path = ToNormalPath(key_str->GetValue());
                auto new_path = ToNormalPath(path.lexically_relative(subdir));
                if (not new_path.is_absolute() and *new_path.begin() != "..") {
                    auto new_key = new_path.string();
                    auto inserted = seen.emplace(new_key, val_node);
                    if (not inserted.second) {
                        auto equal_value =
                            CheckEqual(inserted.first->second, val_node);
                        if (not equal_value) {
                            // might evaluate same, needs runtime evaluation
                            needs_runtime_eval = true;
                            break;
                        }
                        if (not *equal_value) {
                            throw BuiltInError{
                                loc, "Staging conflict for path " + new_key};
                        }
                    }
                    else {
                        out_fields.emplace_back(
                            std::make_shared<StringNode>(loc, new_key),
                            val_node);
                    }
                }
            }

            if (not needs_runtime_eval) {
                return std::make_shared<MapNode>(loc, std::move(out_fields));
            }
        }
    }

    MapNode::fields_t fields{
        {std::make_shared<StringNode>(loc, "type"),
         std::make_shared<StringNode>(loc, "from_subdir")},
        {std::make_shared<StringNode>(loc, "$1"), map_node}};

    if (subdir_node != kDotPath) {
        fields.emplace_back(std::make_shared<StringNode>(loc, "subdir"),
                            subdir_node);
    }

    return std::make_shared<VerbatimNode>(
        loc,
        std::make_shared<MapNode>(loc, std::move(fields)),
        VerbatimType::Flat,
        ValueType::Map);
}

auto ProvideBuiltinsToAST(ASTNodePtr const& ast) -> ASTNodePtr {
    static auto const kBuiltinObj = MakeBuiltinObj();
    // prepend object with built-ins as namespace 'jst' to passed ast
    return std::make_shared<LetNode>(Location{}, "jst", kBuiltinObj, ast);
}

}  // namespace justlang

namespace {

auto BuiltInError::MakeMessage(justlang::Location const& loc,
                               std::string message) -> std::string {
    std::string result;
    if (not loc.file.empty()) {
        result += loc.file + ":";
    }
    result += std::to_string(loc.line) + ":" + std::to_string(loc.column) +
              ": " + std::move(message);
    return result;
}

template <std::size_t N>
[[nodiscard]] auto MakeNamedParameters(
    justlang::CallNode::params_t const& source_args,
    std::array<ExpectedParameter, N> const& expected_args) -> NamedParams {
    NamedParams named;
    named.reserve(expected_args.size());

    // Gather arguments and name nameless arguments:
    std::size_t nameless_number = 0;
    for (auto const& [arg_name, arg_node] : source_args) {
        std::string const* name = nullptr;
        if (arg_name) {
            name = &arg_name.value();
        }
        else {
            if (nameless_number >= expected_args.size()) {
                throw std::runtime_error("Too many nameless parameters");
            }
            name = &expected_args.at(nameless_number++).Name;
        }

        if (not named.insert_or_assign(*name, arg_node).second) {
            throw std::runtime_error(std::string("Parameter \"") + *name +
                                     "\" is duplicated");
        }
    }

    // Check all expected arguments are present and have the expected type:
    for (auto const& expected_arg : expected_args) {
        auto name_it = named.find(expected_arg.Name);
        if (name_it == named.end() or not name_it->second) {
            if (not expected_arg.Mandatory) {
                continue;
            }
            throw std::runtime_error(
                std::string("An expected mandatory parameter \"") +
                expected_arg.Name + "\" is missing.");
        }

        auto const eval_type = name_it->second->EvalType();
        if (not expected_arg.Types.empty() and
            not expected_arg.Types.contains(eval_type)) {
            throw std::runtime_error(std::string("\"") + expected_arg.Name +
                                     "\" does not accept argument type " +
                                     TypeToString(eval_type) + ".");
        }
    }
    return named;
}

template <typename T>
[[nodiscard]] auto RetrieveAs(NamedParams const& params,
                              ExpectedParameter const& to_retrieve)
    -> std::shared_ptr<T> {
    auto param = params.find(to_retrieve.Name);
    if (param == params.end()) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<T>(param->second);
}

[[nodiscard]] auto NodeToJson(justlang::ASTNodePtr const& node,
                              bool json_only) -> std::optional<nlohmann::json> {
    try {
        return justlang::ASTToJsonVisitor{justlang::VerbatimType::Full,
                                          json_only}
            .ToJson(*node);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] auto ProduceSameJSON(justlang::ASTNodePtr const& lhs,
                                   justlang::ASTNodePtr const& rhs,
                                   bool json_only) -> std::optional<bool> {
    auto json_lhs = NodeToJson(lhs, json_only);
    auto json_rhs = NodeToJson(rhs, json_only);
    if (json_lhs and json_rhs) {
        return *json_lhs == *json_rhs;
    }
    return std::nullopt;
}

[[nodiscard]] auto CheckEqual(justlang::ASTNodePtr const& lhs_node,
                              justlang::ASTNodePtr const& rhs_node)
    -> std::optional<bool> {
    if (lhs_node.get() == rhs_node.get()) {
        // exactly same node
        return true;
    }
    if (lhs_node->EvalType() != rhs_node->EvalType() and
        lhs_node->EvalType() != justlang::ValueType::Any and
        rhs_node->EvalType() != justlang::ValueType::Any) {
        // not same eval type
        return false;
    }
    auto lhs_var = lhs_node->ToVariant();
    auto rhs_var = rhs_node->ToVariant();
    if (lhs_var.index() == rhs_var.index()) {  // same node types
        // check if they contain same json (nullopt on non-literal nodes)
        if (auto verdict =
                ProduceSameJSON(lhs_node, rhs_node, /*json_only=*/true)) {
            return verdict;
        }
        // check if they contain same json AST
        if (ProduceSameJSON(lhs_node, rhs_node, /*json_only=*/false)
                .value_or(false)) {
            // exact same json AST
            return true;
        }
        // not the same AST, but might be same after evaluation
    }
    // we cannot decide
    return std::nullopt;
}

[[nodiscard]] auto MakeBuiltinObjEntry(std::string const& name)
    -> justlang::MapNode::entry_t {
    return justlang::MapNode::entry_t{
        std::make_shared<justlang::StringNode>(justlang::Location{}, name),
        std::make_shared<justlang::BuiltinNode>(name)};
}

[[nodiscard]] auto MakeBuiltinObj() -> justlang::ASTNodePtr {
    using justlang::ValueType;
    auto entries = justlang::MapNode::fields_t{};
    entries.emplace_back(MakeBuiltinObjEntry("env"));
    entries.emplace_back(MakeBuiltinObjEntry("get"));
    entries.emplace_back(MakeBuiltinObjEntry("join"));
    entries.emplace_back(MakeBuiltinObjEntry("flatten"));
    entries.emplace_back(MakeBuiltinObjEntry("union"));
    entries.emplace_back(MakeBuiltinObjEntry("keys"));
    entries.emplace_back(MakeBuiltinObjEntry("fail"));
    entries.emplace_back(MakeBuiltinObjEntry("json_encode"));
    entries.emplace_back(MakeBuiltinObjEntry("file"));
    entries.emplace_back(MakeBuiltinObjEntry("symlink"));
    entries.emplace_back(MakeBuiltinObjEntry("tree"));
    entries.emplace_back(MakeBuiltinObjEntry("glob"));
    entries.emplace_back(MakeBuiltinObjEntry("at"));
    entries.emplace_back(MakeBuiltinObjEntry("all"));
    entries.emplace_back(MakeBuiltinObjEntry("any"));
    entries.emplace_back(MakeBuiltinObjEntry("sum"));
    entries.emplace_back(MakeBuiltinObjEntry("prod"));
    entries.emplace_back(MakeBuiltinObjEntry("eq"));
    entries.emplace_back(MakeBuiltinObjEntry("not"));
    entries.emplace_back(MakeBuiltinObjEntry("foreach"));
    entries.emplace_back(MakeBuiltinObjEntry("foldl"));
    entries.emplace_back(MakeBuiltinObjEntry("nub_right"));
    entries.emplace_back(MakeBuiltinObjEntry("nub_left"));
    entries.emplace_back(MakeBuiltinObjEntry("range"));
    entries.emplace_back(MakeBuiltinObjEntry("reverse"));
    entries.emplace_back(MakeBuiltinObjEntry("length"));
    entries.emplace_back(MakeBuiltinObjEntry("basename"));
    entries.emplace_back(MakeBuiltinObjEntry("join_cmd"));
    entries.emplace_back(MakeBuiltinObjEntry("change_ending"));
    entries.emplace_back(MakeBuiltinObjEntry("escape_chars"));
    entries.emplace_back(MakeBuiltinObjEntry("enumerate"));
    entries.emplace_back(MakeBuiltinObjEntry("to_subdir"));
    entries.emplace_back(MakeBuiltinObjEntry("from_subdir"));
    return std::make_shared<justlang::MapNode>(justlang::Location{}, entries);
}

[[nodiscard]] auto NodeToNumber(justlang::ASTNodePtr const& node)
    -> std::optional<double> {
    using justlang::ASTNode;
    using justlang::ValueType;
    if (auto const* num =
            ASTNode::Cast<justlang::NumberNode const*>(node.get())) {
        return num->GetValue();
    }
    if (auto const* str =
            ASTNode::Cast<justlang::StringNode const*>(node.get())) {
        try {
            return std::stod(str->GetValue());
        } catch (...) {
            return 0.0;
        }
    }
    if (ASTNode::Cast<justlang::NullNode const*>(node.get()) != nullptr or
        ASTNode::Cast<justlang::BoolNode const*>(node.get()) != nullptr or
        ASTNode::Cast<justlang::ListNode const*>(node.get()) != nullptr or
        ASTNode::Cast<justlang::MapNode const*>(node.get()) != nullptr) {
        return 0.0;  // JSON literals
    }
    if (node->EvalType() != ValueType::Any and
        node->EvalType() != ValueType::Number and
        node->EvalType() != ValueType::String) {
        return 0.0;  // runtime expr evaluating to unsupported type
    }
    return std::nullopt;
}

[[nodiscard]] auto ShellQuote(std::string arg) -> std::string {
    auto start_pos = size_t{};
    std::string const from{"'"};
    std::string const replacement{"'\\''"};
    while ((start_pos = arg.find(from, start_pos)) != std::string::npos) {
        arg.replace(start_pos, from.length(), replacement);
        start_pos += replacement.length();
    }
    std::ostringstream oss{};
    oss << "'" << arg << "'";
    return oss.str();
}

[[nodiscard]] auto ToNormalPath(std::filesystem::path const& path) noexcept
    -> std::filesystem::path {
    auto norm = path.lexically_normal();
    if (not norm.has_filename()) {
        norm = norm.parent_path();
    }
    if (norm.empty()) {
        return ".";
    }
    return norm;
}

}  // namespace
