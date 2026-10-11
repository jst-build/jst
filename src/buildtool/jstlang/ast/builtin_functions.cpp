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

#include "src/buildtool/jstlang/ast/builtin_functions.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "nlohmann/json.hpp"
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/scope.hpp"
#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"

namespace {

auto const kNullNode = std::make_shared<jstlang::NullNode>(jstlang::Location{});

auto const kEmptyString =
    std::make_shared<jstlang::StringNode>(jstlang::Location{}, std::string{});

auto const kDotPath =
    std::make_shared<jstlang::StringNode>(jstlang::Location{}, ".");

auto const kFalseBool =
    std::make_shared<jstlang::BoolNode>(jstlang::Location{}, false);

// Names of bindings for passing evaluated values to higher order functions
// (contain a space, so they cannot clash with identifiers)
auto const kRangeName = std::string{"jst range"};
auto const kRange1Name = std::string{"jst range1"};
auto const kRange2Name = std::string{"jst range2"};
auto const kInitName = std::string{"jst init"};

// Expression reading the value of a binding
[[nodiscard]] auto VarRead(jstlang::Location const& loc,
                           std::string name) -> jstlang::ASTNodePtr {
    return std::make_shared<jstlang::CallNode>(
        loc, jstlang::CallNode::target_t{std::move(name)});
}

class BuiltInError final : public std::runtime_error {
  public:
    explicit BuiltInError(jstlang::Location const& loc, std::string message)
        : std::runtime_error(MakeMessage(loc, std::move(message))) {}

  private:
    static auto MakeMessage(jstlang::Location const& loc,
                            std::string message) -> std::string;
};

using NamedParams = std::unordered_map<std::string, jstlang::ASTNodePtr>;

struct ExpectedParameter final {
    std::string Name;
    // accepted types, empty means all types are accepted
    std::unordered_set<jstlang::ValueType> Types;
    bool Mandatory = false;
};

template <std::size_t kN>
[[nodiscard]] auto MakeNamedParameters(
    jstlang::CallNode::params_t const& source_args,
    std::array<ExpectedParameter, kN> const& expected_args) -> NamedParams;

template <typename T = jstlang::ASTNode>
[[nodiscard]] auto RetrieveAs(NamedParams const& params,
                              ExpectedParameter const& to_retrieve)
    -> std::shared_ptr<T>;

[[nodiscard]] auto NodeToJson(jstlang::ASTNodePtr const& node,
                              bool json_only) -> std::optional<nlohmann::json>;

[[nodiscard]] auto CheckEqual(jstlang::ASTNodePtr const& lhs_node,
                              jstlang::ASTNodePtr const& rhs_node)
    -> std::optional<bool>;

[[nodiscard]] auto SortedLiteralEntries(jstlang::MapNode const& map)
    -> std::optional<jstlang::MapNode::fields_t>;

[[nodiscard]] auto RefEqual(jstlang::RefData const& lhs,
                            jstlang::RefData const& rhs) -> bool;

[[nodiscard]] auto LiteralEqual(jstlang::ASTNode const& lhs,
                                jstlang::ASTNode const& rhs)
    -> std::optional<bool>;

[[nodiscard]] auto MakeBuiltinObjEntry(std::string const& name)
    -> jstlang::MapNode::entry_t;

[[nodiscard]] auto MakeBuiltinObj() -> jstlang::ASTNodePtr;

[[nodiscard]] auto RangeSize(jstlang::ASTNodePtr const& node)
    -> std::optional<std::size_t>;

[[nodiscard]] auto ShellQuote(std::string arg) -> std::string;

[[nodiscard]] auto ToNormalPath(std::filesystem::path const& path) noexcept
    -> std::filesystem::path;

}  // namespace

namespace jstlang {

auto BuiltIn::env(Location const& loc,
                  EvalCallback const& /*unused*/,
                  CallNode::params_t const& args) -> ASTNodePtr {
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
                  EvalCallback const& /*unused*/,
                  CallNode::params_t const& args) -> ASTNodePtr {
    static const std::array kExpected = {
        ExpectedParameter{"key", {ValueType::Any, ValueType::String}, true},
        ExpectedParameter{"map", {}, true},
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

    // like the expression language for maps of unknown type (e.g., unset
    // runtime variables), null is treated as empty map
    if (map_node->EvalType() != ValueType::Any and
        map_node->EvalType() != ValueType::Map) {
        if (map_node->EvalType() == ValueType::Null) {
            return default_node;
        }
        throw BuiltInError(loc,
                           "get: \"map\" does not accept argument type " +
                               TypeToString(map_node->EvalType()) + ".");
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "get",
        BuiltinCallNode::args_t{
            key_node,
            map_node,
            (default_node == kNullNode ? nullptr : default_node)},
        ValueType::Any);
}

auto BuiltIn::join(Location const& loc,
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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
            return std::make_shared<StringNode>(jstlang::Location{},
                                                std::move(out));
        }
    }

    return std::make_shared<BuiltinCallNode>(
        loc,
        "join",
        BuiltinCallNode::args_t{items_node, separator},
        ValueType::String);
}

auto BuiltIn::flatten(Location const& loc,
                      EvalCallback const& /*unused*/,
                      CallNode::params_t const& args) -> ASTNodePtr {
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
            return std::make_shared<ListNode>(jstlang::Location{},
                                              std::move(output_list));
        }
    }

    return std::make_shared<BuiltinCallNode>(
        loc, "flatten", BuiltinCallNode::args_t{lists_node}, ValueType::List);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto BuiltIn::map_union(Location const& loc,
                        EvalCallback const& /*unused*/,
                        CallNode::params_t const& args) -> ASTNodePtr {
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
            // union of at most one non-empty map is that map
            ASTNodePtr non_empty{};
            std::size_t num_non_empty{};
            for (auto const& item_node : items_list->GetItems()) {
                if (not ASTNode::Cast<MapNode const*>(item_node.get())
                            ->GetFields()
                            .empty()) {
                    non_empty = item_node;
                    ++num_non_empty;
                }
            }
            if (num_non_empty == 1) {
                return non_empty;
            }
        }

        if (not contains_non_static_maps) {
            // Do worst case allocation
            auto output_fields = MapNode::fields_t{};
            output_fields.reserve(max_num_fields);

            // Auxiliary map for tracking field positions in vector (names
            // are owned by the field nodes)
            auto field_pos =
                std::unordered_map<std::string_view, std::size_t>{};
            field_pos.reserve(max_num_fields);

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
                            field_pos.emplace(name->GetValue(),
                                              output_fields.size() - 1);
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
                return std::make_shared<MapNode>(jstlang::Location{},
                                                 std::move(output_fields));
            }
        }
    }

    // static evaluation failed, fallback to creating runtime expressions

    return std::make_shared<BuiltinCallNode>(
        loc,
        "union",
        BuiltinCallNode::args_t{
            maps_node, disjoint_node == kNullNode ? nullptr : disjoint_node},
        ValueType::Map);
}

auto BuiltIn::keys(Location const& loc,
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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
        // map with literal keys -> generate sorted list of unique keys
        if (auto entries = SortedLiteralEntries(*map_obj)) {
            auto keys = ListNode::items_t{};
            keys.reserve(entries->size());
            for (auto const& [key, _] : *entries) {
                keys.emplace_back(key);
            }
            return std::make_shared<ListNode>(loc, std::move(keys));
        }
    }

    return std::make_shared<BuiltinCallNode>(
        loc, "keys", BuiltinCallNode::args_t{map_node}, ValueType::List);
}

auto BuiltIn::fail(Location const& loc,
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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

    // never evaluated statically, even if the message is a literal: the call
    // might be located in a branch that is not taken at runtime; the evaluator
    // reports the error when it has to fully evaluate this call
    return std::make_shared<BuiltinCallNode>(
        loc, "fail", BuiltinCallNode::args_t{msg_node}, ValueType::Any);
}

auto BuiltIn::json_encode(Location const& loc,
                          EvalCallback const& /*unused*/,
                          CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(loc,
                                             "json_encode",
                                             BuiltinCallNode::args_t{data_node},
                                             ValueType::String);
}

auto BuiltIn::file(Location const& loc,
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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
                      EvalCallback const& /*unused*/,
                      CallNode::params_t const& args) -> ASTNodePtr {
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
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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
                 EvalCallback const& /*unused*/,
                 CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "at",
        BuiltinCallNode::args_t{
            index_node,
            list_node,
            (default_node == kNullNode ? nullptr : default_node)},
        ValueType::Any);
}

auto BuiltIn::all(Location const& loc,
                  EvalCallback const& /*unused*/,
                  CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "all", BuiltinCallNode::args_t{args_node}, ValueType::Bool);
}

auto BuiltIn::any(Location const& loc,
                  EvalCallback const& /*unused*/,
                  CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "any", BuiltinCallNode::args_t{args_node}, ValueType::Bool);
}

auto BuiltIn::sum(Location const& loc,
                  EvalCallback const& /*unused*/,
                  CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "sum", BuiltinCallNode::args_t{numbers_node}, ValueType::Number);
}

auto BuiltIn::prod(Location const& loc,
                   EvalCallback const& /*unused*/,
                   CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "prod", BuiltinCallNode::args_t{numbers_node}, ValueType::Number);
}

auto BuiltIn::eq(Location const& loc,
                 EvalCallback const& /*unused*/,
                 CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "eq",
        BuiltinCallNode::args_t{lhs_node, rhs_node},
        ValueType::Bool);
}

auto BuiltIn::negate(Location const& loc,
                     EvalCallback const& /*unused*/,
                     CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "not", BuiltinCallNode::args_t{expr_node}, ValueType::Bool);
}

auto BuiltIn::zip_with(Location const& loc,
                       EvalCallback const& eval_callback,
                       CallNode::params_t const& args) -> ASTNodePtr {
    static const std::array kExpected = {
        ExpectedParameter{"func", {}, true},
        ExpectedParameter{"range1", {ValueType::Any, ValueType::List}, true},
        ExpectedParameter{"range2", {ValueType::Any, ValueType::List}, true},
    };

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("zip_with: ") + e.what());
    }

    auto closure = RetrieveAs<ClosureNode>(named_params, kExpected[0]);
    if (closure == nullptr) {
        throw BuiltInError(loc,
                           "zip_with: Mandatory parameter \"func\" is missing "
                           "or not a function");
    }

    ASTNodePtr range1_node = RetrieveAs(named_params, kExpected[1]);
    if (range1_node == nullptr) {
        throw BuiltInError(loc,
                           "zip_with: Missing mandatory parameter \"range1\"");
    }

    ASTNodePtr range2_node = RetrieveAs(named_params, kExpected[2]);
    if (range2_node == nullptr) {
        throw BuiltInError(loc,
                           "zip_with: Missing mandatory parameter \"range2\"");
    }

    auto const& func = closure->GetFunc();
    if (func.GetParams().size() != 2) {
        throw BuiltInError(
            loc,
            "zip_with: Parameter \"func\" must accept exactly two parameters.");
    }

    auto const& iter1_var = func.GetParams().at(0).first;
    auto const& iter2_var = func.GetParams().at(1).first;
    auto const& body = func.GetBody();
    // evaluate in the closure's scope, ranges are already evaluated
    auto scope = Scope::BindValue(
        closure->GetScope(), kRange1Name, std::move(range1_node));
    scope =
        Scope::BindValue(std::move(scope), kRange2Name, std::move(range2_node));
    return eval_callback(
        std::make_shared<ZipWithNode>(loc,
                                      iter1_var,
                                      iter2_var,
                                      VarRead(loc, kRange1Name),
                                      VarRead(loc, kRange2Name),
                                      body),
        scope);
}

auto BuiltIn::zip_map(Location const& loc,
                      EvalCallback const& /*unused*/,
                      CallNode::params_t const& args) -> ASTNodePtr {
    static const std::array kExpected = {
        ExpectedParameter{"range_key", {ValueType::Any, ValueType::List}, true},
        ExpectedParameter{"range_val", {ValueType::Any, ValueType::List}, true},
    };
    static auto const kKeyTypes =
        std::unordered_set<ValueType>{ValueType::Any, ValueType::String};

    NamedParams named_params;
    try {
        named_params = MakeNamedParameters(args, kExpected);
    } catch (const std::exception& e) {
        throw BuiltInError(loc, std::string("zip_map: ") + e.what());
    }

    ASTNodePtr range_key_node = RetrieveAs(named_params, kExpected[0]);
    if (range_key_node == nullptr) {
        throw BuiltInError(
            loc, "zip_map: Missing mandatory parameter \"range_key\"");
    }

    ASTNodePtr range_val_node = RetrieveAs(named_params, kExpected[1]);
    if (range_val_node == nullptr) {
        throw BuiltInError(
            loc, "zip_map: Missing mandatory parameter \"range_val\"");
    }

    // attempt partial evaluation
    auto const* list_key = ASTNode::Cast<ListNode const*>(range_key_node.get());
    auto const* list_val = ASTNode::Cast<ListNode const*>(range_val_node.get());
    if (list_key != nullptr and list_val != nullptr) {
        // literal arrays -> do unrolling to inline all variable references
        auto const count =
            std::min(list_key->GetItems().size(), list_val->GetItems().size());
        auto out_fields = MapNode::fields_t{};
        out_fields.reserve(count);
        for (std::size_t i{}; i < count; ++i) {
            auto const& key_node = list_key->GetItems()[i];
            auto const& val_node = list_val->GetItems()[i];
            if (not kKeyTypes.contains(key_node->EvalType())) {
                throw BuiltInError(
                    loc,
                    "zip_map: List 'range_key' may not contain items of type " +
                        TypeToString(key_node->EvalType()));
            }
            out_fields.emplace_back(key_node, val_node);
        }
        return std::make_shared<MapNode>(
            jstlang::Location{}, std::move(out_fields), MapNode::Unique::Maybe);
    }
    if (list_key != nullptr and list_key->GetItems().empty()) {
        return std::make_shared<MapNode>(jstlang::Location{},
                                         MapNode::fields_t{});
    }
    if (list_val != nullptr and list_val->GetItems().empty()) {
        return std::make_shared<MapNode>(jstlang::Location{},
                                         MapNode::fields_t{});
    }

    return std::make_shared<BuiltinCallNode>(
        loc,
        "zip_map",
        BuiltinCallNode::args_t{range_key_node, range_val_node},
        ValueType::Map);
}

auto BuiltIn::foreach (Location const& loc,
                       EvalCallback const& eval_callback,
                       CallNode::params_t const& args) -> ASTNodePtr {
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

    auto closure = RetrieveAs<ClosureNode>(named_params, kExpected[0]);
    if (closure == nullptr) {
        throw BuiltInError(loc,
                           "foreach: Mandatory parameter \"func\" is missing "
                           "or not a function");
    }

    ASTNodePtr range_node = RetrieveAs(named_params, kExpected[1]);
    if (range_node == nullptr) {
        throw BuiltInError(loc,
                           "foreach: Missing mandatory parameter \"range\"");
    }

    auto const& func = closure->GetFunc();
    if (func.GetParams().size() != 1) {
        throw BuiltInError(
            loc,
            "foreach: Parameter \"func\" must accept exactly one parameter.");
    }

    auto const& iter_var = func.GetParams().at(0).first;
    auto const& body = func.GetBody();
    // evaluate in the closure's scope, the range is already evaluated
    auto scope = Scope::BindValue(
        closure->GetScope(), kRangeName, std::move(range_node));
    return eval_callback(std::make_shared<ForEachNode>(
                             loc, iter_var, VarRead(loc, kRangeName), body),
                         scope);
}

auto BuiltIn::foldl(Location const& loc,
                    EvalCallback const& eval_callback,
                    CallNode::params_t const& args) -> ASTNodePtr {
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

    auto closure = RetrieveAs<ClosureNode>(named_params, kExpected[0]);
    if (closure == nullptr) {
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

    auto const& func = closure->GetFunc();
    if (func.GetParams().size() != 2) {
        throw BuiltInError(
            loc,
            "foldl: Parameter \"func\" must accept exactly two parameters.");
    }

    auto const& iter_var = func.GetParams().at(0).first;
    auto const& accu_var = func.GetParams().at(1).first;
    auto const& body = func.GetBody();
    // evaluate in the closure's scope, init and range are already evaluated
    auto scope =
        Scope::BindValue(closure->GetScope(), kInitName, std::move(init_node));
    scope =
        Scope::BindValue(std::move(scope), kRangeName, std::move(range_node));
    return eval_callback(
        std::make_shared<FoldLeftNode>(loc,
                                       iter_var,
                                       accu_var,
                                       VarRead(loc, kInitName),
                                       VarRead(loc, kRangeName),
                                       body),
        scope);
}

auto BuiltIn::nub_right(Location const& loc,
                        EvalCallback const& /*unused*/,
                        CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "nub_right", BuiltinCallNode::args_t{list_node}, ValueType::List);
}

auto BuiltIn::nub_left(Location const& loc,
                       EvalCallback const& /*unused*/,
                       CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "nub_left", BuiltinCallNode::args_t{list_node}, ValueType::List);
}

auto BuiltIn::range(Location const& loc,
                    EvalCallback const& /*unused*/,
                    CallNode::params_t const& args) -> ASTNodePtr {
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
    if (auto list_range = RangeSize(size_node)) {
        auto items = ListNode::items_t{};
        items.reserve(*list_range);

        for (std::size_t i{}; i < *list_range; ++i) {
            items.emplace_back(
                std::make_shared<StringNode>(loc, std::to_string(i)));
        }
        return std::make_shared<ListNode>(loc, std::move(items));
    }

    return std::make_shared<BuiltinCallNode>(
        loc, "range", BuiltinCallNode::args_t{size_node}, ValueType::List);
}

auto BuiltIn::reverse(Location const& loc,
                      EvalCallback const& /*unused*/,
                      CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "reverse", BuiltinCallNode::args_t{list_node}, ValueType::List);
}

auto BuiltIn::length(Location const& loc,
                     EvalCallback const& /*unused*/,
                     CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "length", BuiltinCallNode::args_t{list_node}, ValueType::Number);
}

auto BuiltIn::basename(Location const& loc,
                       EvalCallback const& /*unused*/,
                       CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "basename", BuiltinCallNode::args_t{path_node}, ValueType::String);
}

auto BuiltIn::join_cmd(Location const& loc,
                       EvalCallback const& /*unused*/,
                       CallNode::params_t const& args) -> ASTNodePtr {
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
            return std::make_shared<StringNode>(jstlang::Location{},
                                                std::move(out));
        }
    }

    return std::make_shared<BuiltinCallNode>(
        loc, "join_cmd", BuiltinCallNode::args_t{args_node}, ValueType::String);
}

auto BuiltIn::change_ending(Location const& loc,
                            EvalCallback const& /*unused*/,
                            CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "change_ending",
        BuiltinCallNode::args_t{
            path_node, (ending_node == kEmptyString ? nullptr : ending_node)},
        ValueType::String);
}

auto BuiltIn::escape_chars(Location const& loc,
                           EvalCallback const& /*unused*/,
                           CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "escape_chars",
        BuiltinCallNode::args_t{
            str_node,
            (chars_node == kEmptyString ? nullptr : chars_node),
            (prefix_node == kPrefixString ? nullptr : prefix_node)},
        ValueType::String);
}

auto BuiltIn::enumerate(Location const& loc,
                        EvalCallback const& /*unused*/,
                        CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc, "enumerate", BuiltinCallNode::args_t{items_node}, ValueType::Map);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto BuiltIn::to_subdir(Location const& loc,
                        EvalCallback const& /*unused*/,
                        CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "to_subdir",
        BuiltinCallNode::args_t{
            map_node,
            (subdir_node == kDotPath ? nullptr : subdir_node),
            (flat_node == kFalseBool ? nullptr : flat_node),
            msg_node},
        ValueType::Map);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto BuiltIn::from_subdir(Location const& loc,
                          EvalCallback const& /*unused*/,
                          CallNode::params_t const& args) -> ASTNodePtr {
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

    return std::make_shared<BuiltinCallNode>(
        loc,
        "from_subdir",
        BuiltinCallNode::args_t{
            map_node, (subdir_node == kDotPath ? nullptr : subdir_node)},
        ValueType::Map);
}

auto BuiltinsObject() -> ASTNodePtr const& {
    // per thread, as all evaluations share its nodes (whose reference counts
    // would otherwise contend between threads)
    thread_local auto const kBuiltinObj = MakeBuiltinObj();
    return kBuiltinObj;
}

auto ProvideBuiltinsToAST(ASTNodePtr const& ast) -> ASTNodePtr {
    // prepend object with built-ins as namespace 'jst' to passed ast
    return std::make_shared<LetNode>(Location{}, "jst", BuiltinsObject(), ast);
}

}  // namespace jstlang

namespace {

auto BuiltInError::MakeMessage(jstlang::Location const& loc,
                               std::string message) -> std::string {
    std::string result;
    if (auto file = loc.File(); not file.empty()) {
        result += file + ":";
    }
    result += std::to_string(loc.line) + ":" + std::to_string(loc.column) +
              ": " + std::move(message);
    return result;
}

template <std::size_t kN>
[[nodiscard]] auto MakeNamedParameters(
    jstlang::CallNode::params_t const& source_args,
    std::array<ExpectedParameter, kN> const& expected_args) -> NamedParams {
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
    if constexpr (std::is_same_v<T, jstlang::ASTNode>) {
        return param->second;
    }
    else {
        return jstlang::ASTNode::CastPtr<T>(param->second);
    }
}

[[nodiscard]] auto NodeToJson(jstlang::ASTNodePtr const& node,
                              bool json_only) -> std::optional<nlohmann::json> {
    try {
        return jstlang::ASTToJsonVisitor{jstlang::VerbatimType::Full, json_only}
            .ToJson(*node);
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] auto CheckEqual(jstlang::ASTNodePtr const& lhs_node,
                              jstlang::ASTNodePtr const& rhs_node)
    -> std::optional<bool> {
    if (lhs_node.get() == rhs_node.get()) {
        // exactly same node
        return true;
    }
    using jstlang::ValueType;
    auto const lhs_type = lhs_node->EvalType();
    auto const rhs_type = rhs_node->EvalType();
    // a reference is evaluated to a string or a list
    auto const ref_or_value = [](ValueType ref_type, ValueType type) {
        return ref_type == ValueType::Ref and
               (type == ValueType::String or type == ValueType::List);
    };
    if (lhs_type != rhs_type and lhs_type != ValueType::Any and
        rhs_type != ValueType::Any and not ref_or_value(lhs_type, rhs_type) and
        not ref_or_value(rhs_type, lhs_type)) {
        // not same eval type
        return false;
    }
    if (lhs_node->IsLiteral() and rhs_node->IsLiteral()) {
        return LiteralEqual(*lhs_node, *rhs_node);
    }
    // we cannot decide; even structurally equal expressions are left to the
    // evaluation, as they might fail instead of being equal
    return std::nullopt;
}

[[nodiscard]] auto MakeBuiltinObjEntry(std::string const& name)
    -> jstlang::MapNode::entry_t {
    return jstlang::MapNode::entry_t{
        std::make_shared<jstlang::StringNode>(jstlang::Location{}, name),
        std::make_shared<jstlang::BuiltinNode>(name)};
}

[[nodiscard]] auto MakeBuiltinObj() -> jstlang::ASTNodePtr {
    using jstlang::ValueType;
    auto entries = jstlang::MapNode::fields_t{};
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
    entries.emplace_back(MakeBuiltinObjEntry("zip_with"));
    entries.emplace_back(MakeBuiltinObjEntry("zip_map"));
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
    return std::make_shared<jstlang::MapNode>(jstlang::Location{}, entries);
}

[[nodiscard]] auto RangeSize(jstlang::ASTNodePtr const& node)
    -> std::optional<std::size_t> {
    using jstlang::ASTNode;
    using jstlang::ValueType;
    if (auto const* num =
            ASTNode::Cast<jstlang::NumberNode const*>(node.get())) {
        // round to the nearest integer, non-positive numbers are ignored
        return num->GetValue() > 0.0
                   ? static_cast<std::size_t>(std::lround(num->GetValue()))
                   : std::size_t{};
    }
    if (auto const* str =
            ASTNode::Cast<jstlang::StringNode const*>(node.get())) {
        // integer prefix of the string, negative numbers are ignored
        static constexpr int kDecimal = 10;
        auto const value =
            std::strtoll(str->GetValue().c_str(), nullptr, kDecimal);
        return value > 0 ? static_cast<std::size_t>(value) : std::size_t{};
    }
    if (node->IsLiteral()) {
        return std::size_t{};  // other literals
    }
    if (node->EvalType() != ValueType::Any and
        node->EvalType() != ValueType::Number and
        node->EvalType() != ValueType::String) {
        return std::size_t{};  // runtime expr evaluating to unsupported type
    }
    return std::nullopt;
}

[[nodiscard]] auto SortedLiteralEntries(jstlang::MapNode const& map)
    -> std::optional<jstlang::MapNode::fields_t> {
    using jstlang::ASTNode;
    // keys are sorted, the latest entry wins for duplicate keys
    auto sorted =
        std::map<std::string_view, jstlang::MapNode::entry_t const*>{};
    for (auto const& entry : map.GetFields()) {
        auto const* key =
            ASTNode::Cast<jstlang::StringNode const*>(entry.first.get());
        if (key == nullptr) {
            // computed field names can only be evaluated at runtime
            return std::nullopt;
        }
        sorted.insert_or_assign(key->GetValue(), &entry);
    }
    auto entries = jstlang::MapNode::fields_t{};
    entries.reserve(sorted.size());
    for (auto const& [_, entry] : sorted) {
        entries.emplace_back(*entry);
    }
    return entries;
}

/// \brief Check if two references are equal. References of different kinds
/// are never equal, as their values in the expression language differ (a
/// string for local references, lists of different lengths for the others).
[[nodiscard]] auto RefEqual(jstlang::RefData const& lhs,
                            jstlang::RefData const& rhs) -> bool {
    using jstlang::RefType;
    if (lhs.type != rhs.type or
        lhs.target.value_or("") != rhs.target.value_or("")) {
        return false;
    }
    if (lhs.type == RefType::Local) {
        return true;
    }
    return lhs.module == rhs.module and
           (lhs.type != RefType::Ext or
            lhs.repo.value_or("") == rhs.repo.value_or(""));
}

/// \brief Check if two literals are equal. Undecided (nullopt) if a reference
/// is compared to a string or list, as references are only evaluated to their
/// value (a string or list) in full evaluation.
[[nodiscard]] auto LiteralEqual(jstlang::ASTNode const& lhs,
                                jstlang::ASTNode const& rhs)
    -> std::optional<bool> {
    using jstlang::ASTNode;
    using jstlang::ValueType;
    if (&lhs == &rhs) {
        return true;
    }
    auto const* lref = ASTNode::Cast<jstlang::RefNode const*>(&lhs);
    auto const* rref = ASTNode::Cast<jstlang::RefNode const*>(&rhs);
    if (lref != nullptr and rref != nullptr) {
        return RefEqual(lref->GetRefData(), rref->GetRefData());
    }
    if (lref != nullptr or rref != nullptr) {
        auto const type = lref != nullptr ? rhs.EvalType() : lhs.EvalType();
        if (type == ValueType::String or type == ValueType::List) {
            return std::nullopt;
        }
        return false;
    }
    if (lhs.EvalType() != rhs.EvalType()) {
        return false;
    }
    if (auto const* lstr = ASTNode::Cast<jstlang::StringNode const*>(&lhs)) {
        return lstr->GetValue() ==
               ASTNode::Cast<jstlang::StringNode const*>(&rhs)->GetValue();
    }
    if (auto const* lnum = ASTNode::Cast<jstlang::NumberNode const*>(&lhs)) {
        return lnum->GetValue() ==
               ASTNode::Cast<jstlang::NumberNode const*>(&rhs)->GetValue();
    }
    if (auto const* lbool = ASTNode::Cast<jstlang::BoolNode const*>(&lhs)) {
        return lbool->GetValue() ==
               ASTNode::Cast<jstlang::BoolNode const*>(&rhs)->GetValue();
    }
    // lists and maps: unequal if any pair of items (or keys) is unequal,
    // otherwise undecided if any pair is undecided
    auto result = std::optional<bool>{true};
    auto const merge = [&result](std::optional<bool> const& equal) {
        if (equal and not *equal) {
            return false;
        }
        if (not equal) {
            result = std::nullopt;
        }
        return true;
    };
    if (auto const* llist = ASTNode::Cast<jstlang::ListNode const*>(&lhs)) {
        auto const& litems = llist->GetItems();
        auto const& ritems =
            ASTNode::Cast<jstlang::ListNode const*>(&rhs)->GetItems();
        if (litems.size() != ritems.size()) {
            return false;
        }
        for (std::size_t i{}; i < litems.size(); ++i) {
            if (not merge(LiteralEqual(*litems[i], *ritems[i]))) {
                return false;
            }
        }
        return result;
    }
    if (auto const* lmap = ASTNode::Cast<jstlang::MapNode const*>(&lhs)) {
        auto lentries = SortedLiteralEntries(*lmap);
        auto rentries =
            SortedLiteralEntries(*ASTNode::Cast<jstlang::MapNode const*>(&rhs));
        if (lentries->size() != rentries->size()) {
            return false;
        }
        for (std::size_t i{}; i < lentries->size(); ++i) {
            auto const& [lkey, lvalue] = lentries->at(i);
            auto const& [rkey, rvalue] = rentries->at(i);
            if (not merge(LiteralEqual(*lkey, *rkey)) or
                not merge(LiteralEqual(*lvalue, *rvalue))) {
                return false;
            }
        }
        return result;
    }
    // null
    return true;
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
