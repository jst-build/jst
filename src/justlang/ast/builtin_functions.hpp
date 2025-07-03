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

#ifndef JUSTLANG_JSONNET_BUILTIN_FUNCTIONS_HPP
#define JUSTLANG_JSONNET_BUILTIN_FUNCTIONS_HPP

#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "justlang/ast/ast.hpp"

namespace justlang {

class BuiltIn final {
  public:
    class Result {
        friend class BuiltIn;

      public:
        [[nodiscard]] auto GetNode() const -> ASTNodePtr const& {
            return node_;
        }

        [[nodiscard]] auto NeedsInlining() const -> bool {
            return needs_inlining_;
        }

      private:
        ASTNodePtr node_;
        bool needs_inlining_;

        template <class TNode>
            requires(std::derived_from<TNode, ASTNode>)
        // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
        Result(std::shared_ptr<TNode> expr, bool needs_inlining = false)
            : node_{std::move(expr)}, needs_inlining_{needs_inlining} {}
    };

    using Function =
        std::function<Result(Location const&, CallNode::params_t const&)>;

    [[nodiscard]] static auto env(Location const&,
                                  CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto get(Location const&,
                                  CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto join(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto flatten(Location const&,
                                      CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto map_union(Location const&,
                                        CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto keys(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto fail(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto json_encode(Location const&,
                                          CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto file(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto symlink(Location const&,
                                      CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto tree(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto glob(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto at(Location const&,
                                 CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto all(Location const&,
                                  CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto any(Location const&,
                                  CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto sum(Location const&,
                                  CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto prod(Location const&,
                                   CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto eq(Location const&,
                                 CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto negate(Location const&,
                                     CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto foreach (Location const&,
                                       CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto zip_with(Location const&,
                                       CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto foldl(Location const&,
                                    CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto nub_right(Location const&,
                                        CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto nub_left(Location const&,
                                       CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto range(Location const&,
                                    CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto reverse(Location const&,
                                      CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto length(Location const&,
                                     CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto basename(Location const&,
                                       CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto join_cmd(Location const&,
                                       CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto change_ending(Location const&,
                                            CallNode::params_t const&)
        -> Result;

    [[nodiscard]] static auto escape_chars(Location const&,
                                           CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto enumerate(Location const&,
                                        CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto to_subdir(Location const&,
                                        CallNode::params_t const&) -> Result;

    [[nodiscard]] static auto from_subdir(Location const&,
                                          CallNode::params_t const&) -> Result;
};

[[nodiscard]] auto ProvideBuiltinsToAST(ASTNodePtr const& ast) -> ASTNodePtr;

auto const kBuiltinFunctions = std::unordered_map<std::string,
                                                  BuiltIn::Function>{
    {"env", BuiltIn::env},          // env(name=<string>,default=<any>)
    {"get", BuiltIn::get},          // get(key=<any>,map=<any>,default=<any>)
    {"join", BuiltIn::join},        // join(items=<any>,sep=<any>)
    {"flatten", BuiltIn::flatten},  // flatten(lists=<any>)
    {"union", BuiltIn::map_union},  // union(maps=<any>, disjoint=<any>)
    {"keys", BuiltIn::keys},        // keys(map=<any>)
    {"fail", BuiltIn::fail},        // fail(msg=<any>)
    {"json_encode", BuiltIn::json_encode},  // json_encode(data=<any>)
    {"file", BuiltIn::file},                // file(path=<string>)
    {"symlink", BuiltIn::symlink},          // symlink(path=<string>)
    {"tree", BuiltIn::tree},                // tree(path=<string>)
    {"glob", BuiltIn::glob},                // glob(pattern=<string>)
    {"at", BuiltIn::at},       // at(index=<string>,list=<list>,default=<any>)
    {"all", BuiltIn::all},     // all(args=<any>)
    {"any", BuiltIn::any},     // any(args=<any>)
    {"sum", BuiltIn::sum},     // sum(numbers=<any>)
    {"prod", BuiltIn::prod},   // prod(numbers=<any>)
    {"eq", BuiltIn::eq},       // eq(lhs=<any>,rhs=<any>)
    {"not", BuiltIn::negate},  // not(expr=<any>)
    {"foreach", BuiltIn::foreach},  // foreach(func=<func>, range=<any>)
    {"zip_with",
     BuiltIn::zip_with},  // zip_with(func=<func>, range1=<any>, range2=<any>)
    {"foldl", BuiltIn::foldl},  // foldl(func=<func>, init=<any>, range=<any>)
    {"nub_right", BuiltIn::nub_right},          // nub_right(list=<list>)
    {"nub_left", BuiltIn::nub_left},            // nub_left(list=<list>)
    {"range", BuiltIn::range},                  // range(size=<string>)
    {"reverse", BuiltIn::reverse},              // reverse(list=<list>)
    {"length", BuiltIn::length},                // length(list=<list>)
    {"basename", BuiltIn::basename},            // basename(path=<string>)
    {"join_cmd", BuiltIn::join_cmd},            // join_cmd(args=<list>)
    {"change_ending", BuiltIn::change_ending},  // change_ending(path=<string>,
                                                // ending=<string>)
    {"escape_chars",
     BuiltIn::escape_chars},  // escape_chars(str=<string>, chars=<string>,
                              // prefix=<string>)
    {"enumerate", BuiltIn::enumerate},  // enumerate(items=<list>)
    {"to_subdir", BuiltIn::to_subdir},  // to_subdir(map=<map>, subdir=<string>,
                                        // flat=<any>, msg=<any>)
    {"from_subdir",
     BuiltIn::from_subdir},  // from_subdir(map=<map>, subdir=<string>)
};

}  // namespace justlang

#endif  // JUSTLANG_JSONNET_BUILTIN_FUNCTIONS_HPP
