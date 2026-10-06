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

#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/preproc/preprocessor.hpp"

namespace {

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

[[nodiscard]] auto Process(std::string const& code,
                           jstlang::FileType type) -> jstlang::ASTNodePtr {
    auto reader =
        [&code](auto const& import_from,
                auto const& /*file_path*/,
                auto const* /*repo*/) -> std::optional<jstlang::FileData> {
        return jstlang::FileData{.location = import_from, .content = code};
    };
    auto preproc = jstlang::Preprocessor{
        std::move(reader),
        [](auto /*type*/, auto const& msg) { std::cerr << msg; }};
    auto ast = preproc.Process(Context(), "", "TARGETS", type);
    REQUIRE(ast);
    return ast;
}

[[nodiscard]] auto Serialize(std::string const& code,
                             jstlang::FileType type) -> nlohmann::json {
    return jstlang::ExprSerializer{type}.Serialize(*Process(code, type));
}

}  // namespace

TEST_CASE("Plain files are serialized as expressions", "[expr_serializer]") {
    auto const* code = "{foo: 'foo'}";
    CHECK(Serialize(code, jstlang::FileType::Plain) == nlohmann::json::parse(R"(
            {"type": "singleton_map", "key": "foo", "value": "foo"})"));
}

TEST_CASE("Targets files", "[expr_serializer]") {
    auto const targets = jstlang::FileType::Targets;

    SECTION("unevaluated fields are serialized verbatim") {
        auto const* code = R"(
          local deps = ['bar'];
          {
            foo: {
              type: @'rules//CC:library',
              arguments_config: ['X'],
              name: {a: 'b'},
              deps: deps,
            },
            bar: {
              type: 'export',
              target: 'foo',
              arguments_config: ['X'],
              fixed_config: {Y: 'y'},
              doc: ['doc'],
              config_doc: {X: ['x']},
            },
            baz: {
              type: 'install',
              files: {a: {b: 'c'}},
            },
          }
        )";
        CHECK(Serialize(code, targets) == nlohmann::json::parse(R"(
          { "foo":
            { "type": ["@", "rules", "CC", "library"]
            , "arguments_config": ["X"]
            , "name": {"type": "singleton_map", "key": "a", "value": "b"}
            , "deps": ["bar"]
            }
          , "bar":
            { "type": "export"
            , "target": "foo"
            , "arguments_config": ["X"]
            , "fixed_config": {"Y": "y"}
            , "doc": ["doc"]
            , "config_doc": {"X": ["x"]}
            }
          , "baz":
            { "type": "install"
            , "files":
              {"a": {"type": "singleton_map", "key": "b", "value": "c"}}
            }
          })"));
    }

    SECTION("function fields are skipped") {
        auto const* code = R"(
          {foo: {type: 'export', target: 'bar', helper: function(x) x}}
        )";
        CHECK(Serialize(code, targets) == nlohmann::json::parse(R"(
          {"foo": {"type": "export", "target": "bar"}})"));
    }

    SECTION("structural errors") {
        // process as plain file, to skip the static analysis for targets
        auto const plain = jstlang::FileType::Plain;
        auto const serializer = jstlang::ExprSerializer{targets};
        CHECK_THROWS_AS(serializer.Serialize(*Process("['foo']", plain)),
                        jstlang::ExprSerializerError);
        CHECK_THROWS_AS(serializer.Serialize(*Process("{foo: 'bar'}", plain)),
                        jstlang::ExprSerializerError);
        CHECK_THROWS_AS(
            serializer.Serialize(*Process("{foo: {type: 42}}", plain)),
            jstlang::ExprSerializerError);
    }
}

TEST_CASE("Unsupported file types", "[expr_serializer]") {
    auto const ast = Process("{}", jstlang::FileType::Plain);
    CHECK_THROWS_AS(
        jstlang::ExprSerializer{jstlang::FileType::Rules}.Serialize(*ast),
        jstlang::ExprSerializerError);
    CHECK_THROWS_AS(
        jstlang::ExprSerializer{jstlang::FileType::Expressions}.Serialize(*ast),
        jstlang::ExprSerializerError);
}
