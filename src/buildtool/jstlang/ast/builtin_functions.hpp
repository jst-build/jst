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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_BUILTIN_FUNCTIONS_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_BUILTIN_FUNCTIONS_HPP

#include <functional>
#include <string>
#include <unordered_map>

#include "src/buildtool/jstlang/ast/ast.hpp"

namespace jstlang {

class BuiltIn final {
  public:
    // inline callback needed for higher order builtin functions
    using EvalCallback = std::function<ASTNodePtr(ASTNodePtr const&)>;
    using Function = std::function<ASTNodePtr(Location const&,
                                              EvalCallback const&,
                                              CallNode::params_t const&)>;

    [[nodiscard]] static auto env(Location const&,
                                  EvalCallback const&,
                                  CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto get(Location const&,
                                  EvalCallback const&,
                                  CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto join(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto flatten(Location const&,
                                      EvalCallback const&,
                                      CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto map_union(Location const&,
                                        EvalCallback const&,
                                        CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto keys(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto fail(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto json_encode(Location const&,
                                          EvalCallback const&,
                                          CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto file(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto symlink(Location const&,
                                      EvalCallback const&,
                                      CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto tree(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto glob(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto at(Location const&,
                                 EvalCallback const&,
                                 CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto all(Location const&,
                                  EvalCallback const&,
                                  CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto any(Location const&,
                                  EvalCallback const&,
                                  CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto sum(Location const&,
                                  EvalCallback const&,
                                  CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto prod(Location const&,
                                   EvalCallback const&,
                                   CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto eq(Location const&,
                                 EvalCallback const&,
                                 CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto negate(Location const&,
                                     EvalCallback const&,
                                     CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto foreach (Location const&,
                                       EvalCallback const&,
                                       CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto zip_with(Location const&,
                                       EvalCallback const&,
                                       CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto zip_map(Location const&,
                                      EvalCallback const&,
                                      CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto foldl(Location const&,
                                    EvalCallback const&,
                                    CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto nub_right(Location const&,
                                        EvalCallback const&,
                                        CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto nub_left(Location const&,
                                       EvalCallback const&,
                                       CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto range(Location const&,
                                    EvalCallback const&,
                                    CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto reverse(Location const&,
                                      EvalCallback const&,
                                      CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto length(Location const&,
                                     EvalCallback const&,
                                     CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto basename(Location const&,
                                       EvalCallback const&,
                                       CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto join_cmd(Location const&,
                                       EvalCallback const&,
                                       CallNode::params_t const&) -> ASTNodePtr;

    [[nodiscard]] static auto change_ending(Location const&,
                                            EvalCallback const&,
                                            CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto escape_chars(Location const&,
                                           EvalCallback const&,
                                           CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto enumerate(Location const&,
                                        EvalCallback const&,
                                        CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto to_subdir(Location const&,
                                        EvalCallback const&,
                                        CallNode::params_t const&)
        -> ASTNodePtr;

    [[nodiscard]] static auto from_subdir(Location const&,
                                          EvalCallback const&,
                                          CallNode::params_t const&)
        -> ASTNodePtr;
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
    {"zip_map", BuiltIn::zip_map},  // zip_map(range1=<any>, range2=<any>)
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

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_BUILTIN_FUNCTIONS_HPP
