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

#include "src/buildtool/jstlang/ast/traversal.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/parser.hpp"

namespace {

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

[[nodiscard]] auto Parse(std::string const& code) -> jstlang::ASTNodePtr {
    auto reader = [](auto const& /*import_from*/,
                     auto const& /*file_path*/,
                     auto const* /*repo*/) { return std::nullopt; };
    auto parser = jstlang::Parser{Context(), std::move(reader)};
    return parser.ParseData(jstlang::FileData{
        .location = jstlang::FileLocation{.repo = "", .path = "<stdin>"},
        .content = code});
}

/// \brief Number of nodes in the tree.
[[nodiscard]] auto CountNodes(jstlang::ASTNode const& node) -> std::size_t {
    std::size_t count{1};
    jstlang::ForEachChild(node, [&count](jstlang::ASTNodePtr const& child) {
        count += CountNodes(*child);
    });
    return count;
}

/// \brief Replace all string literals "a" by "b".
[[nodiscard]] auto ReplaceA(jstlang::ASTNodePtr const& node)
    -> jstlang::ASTNodePtr {
    if (auto const* str =
            jstlang::ASTNode::Cast<jstlang::StringNode const*>(node.get())) {
        if (str->GetValue() == "a") {
            return std::make_shared<jstlang::StringNode>(node->GetLocation(),
                                                         "b");
        }
        return node;
    }
    return jstlang::MapChildren(node, ReplaceA);
}

}  // namespace

TEST_CASE("traversal", "[for_each_child]") {
    // map: 2 fields (key + value each), list with 3 items
    CHECK(CountNodes(*Parse("{x: [1, 2, 3], y: 'a'}")) == 1 + 2 + 1 + 3 + 1);
    // if: condition, then, else
    CHECK(CountNodes(*Parse("if true then 'a' else 'b'")) == 4);
    // local: value and next; function: default value and body
    CHECK(CountNodes(*Parse("local f(x='a') = x; f()")) > 4);
}

TEST_CASE("traversal", "[map_children]") {
    SECTION("unchanged tree keeps its identity") {
        auto ast = Parse(
            "local f(x) = [x, {k: if x then 1 else 2}]; "
            "[f(true), [y for y in ['c', 'd']]]");
        auto mapped = jstlang::MapChildren(
            ast, [](jstlang::ASTNodePtr const& child) { return child; });
        CHECK(mapped == ast);
        CHECK(ReplaceA(ast) == ast);
    }

    SECTION("changed subtrees are rebuilt, others are kept") {
        auto ast = Parse("[['a', 'c'], ['d']]");
        auto mapped = ReplaceA(ast);
        REQUIRE(mapped != ast);
        auto const* list =
            jstlang::ASTNode::Cast<jstlang::ListNode const*>(mapped.get());
        REQUIRE(list != nullptr);
        auto const* orig =
            jstlang::ASTNode::Cast<jstlang::ListNode const*>(ast.get());
        // first item changed, second item is the same node
        CHECK(list->GetItems()[0] != orig->GetItems()[0]);
        CHECK(list->GetItems()[1] == orig->GetItems()[1]);
        auto const* inner = jstlang::ASTNode::Cast<jstlang::ListNode const*>(
            list->GetItems()[0].get());
        REQUIRE(inner != nullptr);
        auto const* str = jstlang::ASTNode::Cast<jstlang::StringNode const*>(
            inner->GetItems()[0].get());
        REQUIRE(str != nullptr);
        CHECK(str->GetValue() == "b");
    }

    SECTION("all node kinds are rebuilt with their properties") {
        auto ast = Parse(
            "local v = 'a'; "
            "{[v]: -1, z: !true, w: 'a' + 'c', u: {a: 'a'}.a, "
            "t: [x for x in ['a']], s: jst.foldl(function(x, y) 'a', 'a', "
            "[]), r: 'a' == 'b', q: jst.env('a', default='a')}");
        auto mapped = ReplaceA(ast);
        REQUIRE(mapped != ast);
        CHECK(mapped->Kind() == ast->Kind());
        CHECK(CountNodes(*mapped) == CountNodes(*ast));
        // no string literal "a" left
        std::function<void(jstlang::ASTNode const&)> check_no_a =
            [&check_no_a](jstlang::ASTNode const& node) {
                if (auto const* str =
                        jstlang::ASTNode::Cast<jstlang::StringNode const*>(
                            &node)) {
                    CHECK(str->GetValue() != "a");
                }
                jstlang::ForEachChild(
                    node, [&check_no_a](jstlang::ASTNodePtr const& child) {
                        check_no_a(*child);
                    });
            };
        check_no_a(*mapped);
    }
}
