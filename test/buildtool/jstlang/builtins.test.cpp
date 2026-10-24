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

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/evaluator.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"
#include "src/buildtool/jstlang/evaluator.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/parser.hpp"

namespace {

/// \brief Built-in call with placeholders "$NAME" and their values (JSON).
struct Case final {
    std::string code;
    std::string values;
    bool fails = false;
};

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

[[nodiscard]] auto ParseAndInline(std::string const& code)
    -> jstlang::ASTNodePtr {
    auto parser = jstlang::Parser{Context(), /*reader=*/nullptr};
    auto ast = parser.ParseData(
        jstlang::FileLocation{.repo = "", .path = "<stdin>"}, code);
    return jstlang::Evaluator{
        jstlang::EvalOptions{.mode = jstlang::EvalOptions::Mode::Lowering}}
        .EvaluateAST(ast);
}

/// \brief Replace each placeholder by the given text.
template <class TReplacement>
[[nodiscard]] auto Substitute(Case const& c,
                              TReplacement const& replacement) -> std::string {
    auto code = c.code;
    auto const values = nlohmann::json::parse(c.values);
    for (auto const& [name, value] : values.items()) {
        auto const placeholder = "$" + name;
        for (auto pos = code.find(placeholder); pos != std::string::npos;
             pos = code.find(placeholder, pos)) {
            auto text = replacement(name, value);
            code.replace(pos, placeholder.size(), text);
            pos += text.size();
        }
    }
    return code;
}

/// \brief Evaluate with all values as literals, which must be fully evaluated
/// by the built-ins. Returns nullopt if evaluation failed.
[[nodiscard]] auto EvalFolded(Case const& c) -> std::optional<nlohmann::json> {
    auto code = Substitute(c, [](auto const& /*name*/, auto const& value) {
        return value.dump();
    });
    INFO("folded code: " << code);
    jstlang::ASTNodePtr ast{};
    try {
        ast = ParseAndInline(code);
    } catch (...) {
        return std::nullopt;
    }
    try {
        return jstlang::ASTToJsonVisitor{jstlang::VerbatimType::Full,
                                         /*json_only=*/true}
            .ToJson(*ast);
    } catch (...) {
        FAIL("not evaluated to a literal: "
             << jstlang::ASTToJsonVisitor{}.ToJson(*ast).dump());
    }
    return std::nullopt;
}

/// \brief Evaluate with all values from the configuration, via lowering to
/// the expression language. Returns nullopt if evaluation failed.
[[nodiscard]] auto EvalLowered(Case const& c) -> std::optional<nlohmann::json> {
    auto code = Substitute(c, [](auto const& name, auto const& /*value*/) {
        return "jst.env('" + name + "')";
    });
    INFO("lowered code: " << code);
    try {
        auto ast = ParseAndInline(code);
        auto expr =
            Expression::FromJson(jstlang::ASTToJsonVisitor{}.ToJson(*ast));
        auto result = Evaluator::EvaluateExpression(
            expr,
            Configuration{
                Expression::FromJson(nlohmann::json::parse(c.values))},
            FunctionMap::Ptr{nullptr},
            [](auto const& /*unused*/) {});
        if (result) {
            return result->ToJson();
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

void CheckSameResult(std::vector<Case> const& cases) {
    for (auto const& c : cases) {
        INFO("code: " << c.code << "\nvalues: " << c.values);
        auto folded = EvalFolded(c);
        auto lowered = EvalLowered(c);
        CHECK(folded == lowered);
        CHECK(folded.has_value() != c.fails);
    }
}

}  // namespace

TEST_CASE("builtins", "[full_evaluation]") {
    SECTION("get") {
        CheckSameResult({
            {"jst.get($K, $M)", R"({"K": "a", "M": {"a": 1, "b": 2}})"},
            {"jst.get($K, $M)", R"({"K": "c", "M": {"a": 1}})"},
            {"jst.get($K, $M, default=5)", R"({"K": "c", "M": {"a": 1}})"},
            {"jst.get($K, $M, default=5)", R"({"K": "a", "M": {"a": null}})"},
            {"jst.get($K, $M, default=5)", R"({"K": "a", "M": null})"},
        });
    }

    SECTION("join") {
        CheckSameResult({
            {"jst.join($L)", R"({"L": ["a", "b"]})"},
            {"jst.join($L, sep=$S)", R"({"L": ["a", "b", "c"], "S": ", "})"},
            {"jst.join($L, sep='-')", R"({"L": []})"},
        });
    }

    SECTION("flatten") {
        CheckSameResult({
            {"jst.flatten($L)", R"({"L": [[1], [], [2, [3]]]})"},
        });
    }

    SECTION("union") {
        CheckSameResult({
            {"jst.union($L)", R"({"L": []})"},
            {"jst.union($L)", R"({"L": [{"a": 1, "b": 1}, {"b": 2}]})"},
            {"jst.union($L, disjoint=true)",
             R"({"L": [{"a": 1, "b": [1]}, {"b": [1], "c": {}}]})"},
            {"jst.union($L, disjoint=true)",
             R"({"L": [{"a": 1}, {"a": 2}]})",
             /*fails=*/true},
        });
    }

    SECTION("keys") {
        CheckSameResult({
            {"jst.keys($M)", R"({"M": {"b": 1, "a": 2, "c": 3}})"},
            {"local k = 'b'; jst.keys({b: 1, a: 2, [k]: $X})", R"({"X": 3})"},
        });
    }

    SECTION("json_encode") {
        CheckSameResult({
            {"jst.json_encode($D)",
             R"({"D": {"b": [1, "x", null, true, 2.5], "a": {}}})"},
        });
    }

    SECTION("at") {
        CheckSameResult({
            {"jst.at($I, $L)", R"({"I": 1, "L": ["a", "b"]})"},
            {"jst.at($I, $L)", R"({"I": -1, "L": ["a", "b"]})"},
            {"jst.at($I, $L)", R"({"I": 1.6, "L": ["a", "b"]})"},
            {"jst.at($I, $L)", R"({"I": "1", "L": ["a", "b"]})"},
            {"jst.at($I, $L, default='d')", R"({"I": 5, "L": ["a", "b"]})"},
            {"jst.at($I, $L, default='d')", R"({"I": -3, "L": ["a", "b"]})"},
        });
    }

    SECTION("all and any") {
        CheckSameResult({
            {"jst.all($L)", R"({"L": [true, 1, "x", [0], {"a": 1}]})"},
            {"jst.all($L)", R"({"L": [true, ""]})"},
            {"jst.all($L)", R"({"L": []})"},
            {"jst.any($L)", R"({"L": [null, 0, "", [], {}, false]})"},
            {"jst.any($L)", R"({"L": [null, "x"]})"},
            {"jst.any($L)", R"({"L": []})"},
        });
    }

    SECTION("sum and prod") {
        CheckSameResult({
            {"jst.sum($L)", R"({"L": [1, 2.5, -3]})"},
            {"jst.sum($L)", R"({"L": []})"},
            {"jst.prod($L)", R"({"L": [2, 3]})"},
            {"jst.prod($L)", R"({"L": []})"},
        });
    }

    SECTION("eq") {
        CheckSameResult({
            {"jst.eq($A, $B)",
             R"({"A": {"a": 1, "b": [1, 2]}, "B": {"b": [1, 2], "a": 1}})"},
            {"jst.eq($A, $B)", R"({"A": [1, 2], "B": [2, 1]})"},
            {"jst.eq($A, $B)", R"({"A": [1, 2], "B": [1, 2, 3]})"},
            {"jst.eq($A, $B)", R"({"A": 1, "B": 1.0})"},
            {"jst.eq($A, $B)", R"({"A": "1", "B": 1})"},
            {"jst.eq($A, $B)", R"({"A": null, "B": null})"},
            {"jst.eq($A, $B)", R"({"A": {}, "B": []})"},
            {"jst.eq($A, $B)",
             R"({"A": {"x": {"y": [true]}}, "B": {"x": {"y": [false]}}})"},
            {"local k = 'a'; jst.eq({a: 1, [k]: $X}, {a: 2})", R"({"X": 2})"},
            // references are only equal to references of the same kind
            {"jst.eq(@':t', @':t')", "{}"},
            {"jst.eq(@':t', @':u')", "{}"},
            {"jst.eq(@'//m:t', @'//m:t')", "{}"},
            {"jst.eq(@'r//m:t', @'r//m:t')", "{}"},
            {"jst.eq(@'r//m:t', @'//m:t')", "{}"},
            {"jst.eq(@'./m:t', @'./m:t')", "{}"},
            {"jst.eq(@'./m:t', @'//m:t')", "{}"},
            {"jst.eq(@':t', $X)", R"({"X": 1})"},
            {"jst.eq([@'//m:t'], $X)", R"({"X": [null]})"},
        });
    }

    SECTION("not") {
        CheckSameResult({
            {"jst.not($X)", R"({"X": ""})"},
            {"jst.not($X)", R"({"X": [1]})"},
            {"jst.not($X)", R"({"X": 0})"},
        });
    }

    SECTION("zip_map") {
        CheckSameResult({
            {"jst.zip_map($K, $V)",
             R"({"K": ["a", "b", "a"], "V": [1, 2, 3]})"},
            {"jst.zip_map($K, $V)", R"({"K": ["a"], "V": []})"},
            {"jst.get('a', jst.zip_map($K, $V))",
             R"({"K": ["a", "b"], "V": [1, 2]})"},
        });
    }

    SECTION("nub_right and nub_left") {
        auto const* values =
            R"({"L": [1, 2, 1, {"a": 1}, {"a": 1}, [1], "1", [1]]})";
        CheckSameResult({
            {"jst.nub_right($L)", values},
            {"jst.nub_left($L)", values},
        });
    }

    SECTION("range") {
        CheckSameResult({
            {"jst.range($S)", R"({"S": 3})"},
            {"jst.range($S)", R"({"S": 2.5})"},
            {"jst.range($S)", R"({"S": -1})"},
            {"jst.range($S)", R"({"S": "2.7"})"},
            {"jst.range($S)", R"({"S": "  4x"})"},
            {"jst.range($S)", R"({"S": "abc"})"},
            {"jst.range($S)", R"({"S": true})"},
            {"jst.range($S)", R"({"S": null})"},
        });
    }

    SECTION("list functions") {
        CheckSameResult({
            {"jst.reverse($L)", R"({"L": [1, 2, 3]})"},
            {"jst.length($L)", R"({"L": [1, 2, 3]})"},
            {"jst.enumerate($L)", R"({"L": ["a", "b"]})"},
            {"jst.join_cmd($L)", R"({"L": ["a b", "it's"]})"},
        });
    }

    SECTION("string functions") {
        CheckSameResult({
            {"jst.basename($P)", R"({"P": "a/b/c.txt"})"},
            {"jst.basename($P)", R"({"P": "a/b/"})"},
            {"jst.change_ending($P)", R"({"P": "x.y"})"},
            {"jst.change_ending($P, ending=$E)",
             R"({"P": "a/b.c", "E": ".o"})"},
            {"jst.escape_chars($S, chars=$C)",
             R"({"S": "a$b\"c", "C": "$\""})"},
            {"jst.escape_chars($S, chars='a', prefix=$P)",
             R"({"S": "banana", "P": "\\"})"},
        });
    }

    SECTION("to_subdir") {
        CheckSameResult({
            {"jst.to_subdir($M, subdir=$D)",
             R"({"M": {"a/b": 1, "c": 2}, "D": "x"})"},
            {"jst.to_subdir($M, subdir='x', flat=true)",
             R"({"M": {"a/b": 1, "c/b": 1}})"},
            {"jst.to_subdir($M, subdir='x', flat=true)",
             R"({"M": {"a/b": 1, "c/b": 2}})",
             /*fails=*/true},
            {"jst.to_subdir($M, subdir='x')",
             R"({"M": {"a/../b": 1, "b": 2}})",
             /*fails=*/true},
        });
    }

    SECTION("from_subdir") {
        CheckSameResult({
            {"jst.from_subdir($M, subdir=$D)",
             R"({"M": {"x/a": 1, "x/b/c": 2, "y": 3}, "D": "x"})"},
            {"jst.from_subdir($M, subdir='x')",
             R"({"M": {"x/a": 1, "x/./a": 1}})"},
            {"jst.from_subdir($M, subdir='x')",
             R"({"M": {"x/a": 1, "x/./a": 2}})",
             /*fails=*/true},
        });
    }
}

TEST_CASE("builtins", "[map_semantics]") {
    // maps with duplicate keys (only possible via computed keys): the latest
    // entry wins
    auto eval = [](std::string const& code) {
        return EvalFolded(Case{.code = code, .values = "{}"});
    };
    CHECK(eval("local k = 'b'; jst.keys({b: 1, a: 2, [k]: 3})") ==
          nlohmann::json::parse(R"(["a", "b"])"));
    CHECK(eval("local k = 'a'; jst.to_subdir({a: 1, [k]: 2}, subdir='d')") ==
          nlohmann::json::parse(R"({"d/a": 2})"));
    CHECK(eval("local k = 'd/a'; jst.from_subdir({'d/a': 1, [k]: 2}, "
               "subdir='d')") == nlohmann::json::parse(R"({"a": 2})"));
    CHECK(eval("local k = 'a'; jst.eq({a: 1, [k]: 2}, {a: 2})") ==
          nlohmann::json(true));
}

TEST_CASE("builtins", "[residual]") {
    // fail is never evaluated statically
    auto ast = ParseAndInline("jst.fail('msg')");
    auto const* call =
        jstlang::ASTNode::Cast<jstlang::BuiltinCallNode const*>(ast.get());
    REQUIRE(call != nullptr);
    CHECK(call->GetName() == "fail");

    // computed keys cannot be sorted statically
    ast = ParseAndInline("jst.keys({[jst.env('K')]: 1, a: 2})");
    call = jstlang::ASTNode::Cast<jstlang::BuiltinCallNode const*>(ast.get());
    REQUIRE(call != nullptr);
    CHECK(call->GetName() == "keys");

    // references are compared to strings and lists only by their value in
    // full evaluation
    for (auto const* code : {"jst.eq(@':t', 't')",
                             "jst.eq(@'//m:t', ['m', 't'])",
                             "jst.eq([1, @':t'], [1, 't'])",
                             "jst.eq({a: @':t'}, {a: 'u'})"}) {
        INFO("code: " << code);
        ast = ParseAndInline(code);
        call =
            jstlang::ASTNode::Cast<jstlang::BuiltinCallNode const*>(ast.get());
        REQUIRE(call != nullptr);
        CHECK(call->GetName() == "eq");
    }
    // but unequal, if any other items are unequal
    ast = ParseAndInline("jst.eq([2, @':t'], [1, 't'])");
    auto const* result =
        jstlang::ASTNode::Cast<jstlang::BoolNode const*>(ast.get());
    REQUIRE(result != nullptr);
    CHECK(not result->GetValue());
}

TEST_CASE("builtins", "[rule-only functions]") {
    // calls of rule-only functions stay calls (evaluated by the rule analysis),
    // serialized as in the expression language
    SECTION("rule-only functions") {
        auto rule_expression = [](std::string const& expr) {
            return jstlang::ASTToJsonVisitor{}.ToJson(*ParseAndInline(expr));
        };

        CHECK(rule_expression("jst.field('deps')") ==
              nlohmann::json::parse(R"({"type": "FIELD", "name": "deps"})"));

        CHECK(rule_expression("jst.dep_artifacts(jst.env('x'))") ==
              nlohmann::json::parse(R"(
              {"type": "DEP_ARTIFACTS", "dep": {"type": "var", "name": "x"}})"));
        CHECK(rule_expression("jst.dep_artifacts(transition={}, "
                              "dep=jst.env('x'))") == nlohmann::json::parse(R"(
                { "type": "DEP_ARTIFACTS"
                , "dep": {"type": "var", "name": "x"}
                , "transition": {"type": "empty_map"}
                })"));

        CHECK(rule_expression("jst.dep_runfiles(jst.env('x'), {})") ==
              nlohmann::json::parse(R"(
                { "type": "DEP_RUNFILES"
                , "dep": {"type": "var", "name": "x"}
                , "transition": {"type": "empty_map"}
                })"));

        CHECK(rule_expression("jst.dep_provides(jst.env('x'), 'foo')") ==
              nlohmann::json::parse(R"(
                { "type": "DEP_PROVIDES"
                , "dep": {"type": "var", "name": "x"}
                , "provider": "foo"
                })"));
        CHECK(rule_expression("jst.dep_provides(jst.env('x'), 'foo', "
                              "default=null)") == nlohmann::json::parse(R"(
                { "type": "DEP_PROVIDES"
                , "dep": {"type": "var", "name": "x"}
                , "provider": "foo"
                , "default": null
                })"));

        CHECK(rule_expression("jst.make_blob()") ==
              nlohmann::json::parse(R"({"type": "BLOB"})"));
        CHECK(rule_expression("jst.make_blob('data')") ==
              nlohmann::json::parse(R"({"type": "BLOB", "data": "data"})"));

        CHECK(rule_expression("jst.make_symlink('libfoo.so.1.2.3')") ==
              nlohmann::json::parse(
                  R"({"type": "SYMLINK", "data": "libfoo.so.1.2.3"})"));
        CHECK(
            rule_expression("jst.make_symlink(target=jst.env('x'))") ==
            nlohmann::json::parse(
                R"({"type": "SYMLINK", "data": {"type": "var", "name": "x"}})"));

        CHECK(rule_expression("jst.make_tree({})") == nlohmann::json::parse(R"(
                {"type": "TREE", "$1": {"type": "empty_map"}})"));

        CHECK(rule_expression("jst.tree_overlay([])") ==
              nlohmann::json::parse(R"({"type": "TREE_OVERLAY", "$1": []})"));
        CHECK(rule_expression("jst.tree_overlay([], disjoint=true)") ==
              nlohmann::json::parse(
                  R"({"type": "DISJOINT_TREE_OVERLAY", "$1": []})"));

        CHECK(rule_expression(R"(
                jst.action(
                  outs=['out'],
                  cmd=['touch', 'out'],
                  timeout_scaling=2,
                  execution_properties={},
                  may_fail=['test'],
                  fail_message='failed',
                  no_cache=['test'],
                ))") == nlohmann::json::parse(R"(
                  { "type": "ACTION"
                  , "outs": ["out"]
                  , "cmd": ["touch", "out"]
                  , "timeout scaling": 2.0
                  , "execution properties": {"type": "empty_map"}
                  , "may_fail": ["test"]
                  , "fail_message": "failed"
                  , "no_cache": ["test"]
                  })"));
        CHECK(rule_expression(R"(
                jst.action(
                  inputs={},
                  out_dirs=['out'],
                  env={},
                  cwd='work',
                  label='Running',
                ))") == nlohmann::json::parse(R"(
                  { "type": "ACTION"
                  , "inputs": {"type": "empty_map"}
                  , "out_dirs": ["out"]
                  , "env": {"type": "empty_map"}
                  , "cwd": "work"
                  , "label": "Running"
                  })"));

        CHECK(rule_expression("jst.result()") ==
              nlohmann::json::parse(R"({"type": "RESULT"})"));
        CHECK(rule_expression("jst.result({}, {}, {})") ==
              nlohmann::json::parse(R"(
                { "type": "RESULT"
                , "artifacts": {"type": "empty_map"}
                , "runfiles": {"type": "empty_map"}
                , "provides": {"type": "empty_map"}
                })"));

        CHECK(rule_expression("jst.value_node(jst.result())") ==
              nlohmann::json::parse(R"(
                {"type": "VALUE_NODE", "$1": {"type": "RESULT"}})"));

        CHECK(rule_expression("jst.abstract_node('node', target_fields={})") ==
              nlohmann::json::parse(R"(
                { "type": "ABSTRACT_NODE"
                , "node_type": "node"
                , "target_fields": {"type": "empty_map"}
                })"));
    }

    SECTION("errors") {
        // missing mandatory argument
        CHECK_THROWS(ParseAndInline("jst.field()"));
        CHECK_THROWS(ParseAndInline("jst.dep_provides(jst.env('x'))"));
        CHECK_THROWS(ParseAndInline("jst.make_tree()"));
        CHECK_THROWS(ParseAndInline("jst.make_symlink()"));
        // wrong argument type
        CHECK_THROWS(ParseAndInline("jst.action(outs={})"));
        CHECK_THROWS(ParseAndInline("jst.make_symlink([])"));
        CHECK_THROWS(ParseAndInline("jst.dep_artifacts('x')"));
        CHECK_THROWS(ParseAndInline("jst.value_node({})"));
        CHECK_THROWS(ParseAndInline("jst.value_node(jst.make_blob())"));
        // disjoint must be a literal
        CHECK_THROWS(ParseAndInline("jst.tree_overlay([], jst.env('x'))"));
    }

    SECTION("types of results") {
        // results of rule-only functions are typed, e.g., an artifact is not a
        // string
        CHECK_THROWS(ParseAndInline("jst.make_blob('data') + 'x'"));
        CHECK_NOTHROW(ParseAndInline("jst.value_node(jst.result())"));
        CHECK_NOTHROW(
            ParseAndInline("jst.dep_artifacts(jst.abstract_node('n'))"));
    }
}
