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

#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
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

using file_map_t = std::unordered_map<std::string, std::string>;

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

[[nodiscard]] auto Parse(std::string const& code,
                         file_map_t files = {}) -> jstlang::ASTNodePtr {
    auto reader =
        [files = std::move(files)](
            jstlang::FileLocation const& import_from,
            std::filesystem::path const& file_path,
            std::string const* /*repo*/) -> std::optional<jstlang::FileData> {
        auto path = (file_path.is_relative()
                         ? import_from.path.parent_path() / file_path
                         : file_path)
                        .lexically_proximate("/")
                        .lexically_normal();
        if (auto it = files.find(path.string()); it != files.end()) {
            return jstlang::FileData{
                .location = jstlang::FileLocation{.repo = "", .path = path},
                .content = it->second};
        }
        return std::nullopt;
    };
    auto parser = jstlang::Parser{Context(), std::move(reader)};
    return parser.ParseData(jstlang::FileData{
        .location = jstlang::FileLocation{.repo = "", .path = "<stdin>"},
        .content = code});
}

[[nodiscard]] auto ToConfig(nlohmann::json const& json) -> Configuration {
    return Configuration{Expression::FromJson(json)};
}

/// \brief Evaluate via preprocessor (lowering to expression language).
[[nodiscard]] auto EvalLowered(jstlang::ASTNodePtr const& ast,
                               Configuration const& config)
    -> std::optional<nlohmann::json> {
    try {
        auto lowered =
            jstlang::Evaluator{
                jstlang::EvalOptions{.mode =
                                         jstlang::EvalOptions::Mode::Lowering}}
                .EvaluateAST(ast);
        auto expr =
            Expression::FromJson(jstlang::ASTToJsonVisitor{}.ToJson(*lowered));
        auto result = Evaluator::EvaluateExpression(
            expr, config, FunctionMap::Ptr{nullptr}, [](auto const&) {});
        if (result) {
            return result->ToJson();
        }
    } catch (...) {
        // failure is reported as missing result
        return std::nullopt;
    }
    return std::nullopt;
}

/// \brief Evaluate via runtime evaluator.
[[nodiscard]] auto EvalRuntime(jstlang::ASTNodePtr const& ast,
                               Configuration const* config,
                               std::string* error = nullptr)
    -> std::optional<nlohmann::json> {
    auto result = JstlangEvaluator::Evaluate(ast,
                                             jstlang::Bindings::Empty(),
                                             config,
                                             FunctionMap::Ptr{nullptr},
                                             [error](auto const& msg) {
                                                 if (error != nullptr) {
                                                     *error = msg;
                                                 }
                                             });
    if (result) {
        return result->ToJson();
    }
    return std::nullopt;
}

[[nodiscard]] auto EvalRuntime(std::string const& code,
                               nlohmann::json const& config = {},
                               std::string* error = nullptr)
    -> std::optional<nlohmann::json> {
    auto conf = ToConfig(config.is_null() ? nlohmann::json::object() : config);
    return EvalRuntime(Parse(code), &conf, error);
}

/// \brief Check that both evaluation strategies produce the same result in all
/// given configurations.
void CheckSame(std::string const& code,
               std::vector<nlohmann::json> const& configs = {
                   nlohmann::json::object()}) {
    INFO("code: " << code);
    auto ast = Parse(code);
    for (auto const& json : configs) {
        INFO("config: " << json.dump());
        auto config = ToConfig(json);
        auto lowered = EvalLowered(ast, config);
        auto runtime = EvalRuntime(ast, &config);
        REQUIRE(lowered);
        REQUIRE(runtime);
        CHECK(*lowered == *runtime);
    }
}

auto const kConfigs = std::vector<nlohmann::json>{
    nlohmann::json::object(),
    {{"x", "foo"}, {"y", "bar"}, {"n", 3}, {"flag", true}},
    {{"x", ""}, {"l", {"a", "b", "a"}}, {"m", {{"k", "v"}, {"j", nullptr}}}},
};

}  // namespace

TEST_CASE("runtime evaluator", "[literals]") {
    CheckSame("null");
    CheckSame("true");
    CheckSame("42");
    CheckSame("'foo'");
    CheckSame("['foo', 1, null, [true]]");
    CheckSame("{}");
    CheckSame("{foo: 'foo', 'bar baz': [1, 2], ['comp' + 'uted']: {}}");
    CheckSame("{foo: 'a', ['foo']: 'b'}");
    CheckSame("[@':foo', @'./sub:bar', @'//mod:baz', @'repo//mod:qux']");
    CheckSame("|||\n  multi\n  line\n|||");
}

TEST_CASE("runtime evaluator", "[locals and functions]") {
    CheckSame("local a = 'foo'; local b = a + 'bar'; [a, b]");
    CheckSame("local f(x) = [x, x]; f('foo')");
    CheckSame("local f(a, b='B', c='C') = [a, b, c]; f('a', c='c')");
    CheckSame("local f(a, b=a) = [a, b]; f('x')");
    CheckSame(
        "local a = 'outer'; local f(x) = [a, x]; local a = 'inner'; f(a)");
    CheckSame("local f(x) = local g(y) = [x, y]; g('y'); f('x')");
    CheckSame("local f = function(x) [x]; f('x')");
    CheckSame("local m = {f(x): [x], v: 'v'}; [m.f('x'), m.v]");
    CheckSame("local twice(f, x) = f(f(x)); twice(function(x) x + 'a', 'b')");
    CheckSame(
        "local x = jst.env('x'); local y = jst.env('y', default='Y');"
        "[x, y, $'n']",
        kConfigs);
}

TEST_CASE("runtime evaluator", "[control flow]") {
    CheckSame("if jst.env('flag') then 'yes' else 'no'", kConfigs);
    CheckSame("if jst.env('flag') then 'yes'", kConfigs);
    CheckSame("if jst.env('x') == 'foo' then 1 else if jst.env('x') then 2",
              kConfigs);
    CheckSame("[x + '!' for x in ['a', 'b']]");
    CheckSame("[x + '!' for x in jst.env('l', default=[])]", kConfigs);
    CheckSame("{[x]: x + '!' for x in jst.env('l', default=['z'])}", kConfigs);
}

TEST_CASE("runtime evaluator", "[operators]") {
    CheckSame("['a'] + jst.env('l', default=[])", kConfigs);
    CheckSame("'a' + jst.env('x', default='')", kConfigs);
    CheckSame("1 + jst.env('n', default=0)", kConfigs);
    CheckSame("{a: 1} + jst.env('m', default={})", kConfigs);
    CheckSame("2 * jst.env('n', default=1)", kConfigs);
    CheckSame("jst.env('n', default=1) - 4", kConfigs);
    CheckSame("-jst.env('n', default=1)", kConfigs);
    CheckSame("jst.env('x') == 'foo'", kConfigs);
    CheckSame("jst.env('x') != 'foo'", kConfigs);
    CheckSame("jst.env('x') && jst.env('flag')", kConfigs);
    CheckSame("jst.env('x') || jst.env('flag')", kConfigs);
    CheckSame("!jst.env('flag')", kConfigs);
}

TEST_CASE("runtime evaluator", "[lookups]") {
    CheckSame("local m = {a: 'A', 'b c': 'B'}; [m.a, m['b c']]");
    CheckSame("local l = ['a', 'b']; [l[0], l[-1], l['1']]");
}

TEST_CASE("runtime evaluator", "[built-in functions]") {
    CheckSame("jst.get('k', jst.env('m'), default='D')", kConfigs);
    CheckSame("jst.get('j', jst.env('m', default={}), default='D')", kConfigs);
    CheckSame("jst.get('a', {a: 'A'})");
    CheckSame("jst.at(1, jst.env('l', default=[]), default='D')", kConfigs);
    CheckSame("jst.join(['a', jst.env('x', default='')], sep='-')", kConfigs);
    CheckSame("jst.flatten([['a'], jst.env('l', default=[])])", kConfigs);
    CheckSame("jst.union([{a: 'A'}, jst.env('m', default={})])", kConfigs);
    CheckSame("jst.union([{a: 'A'}, {a: 'A'}], disjoint=true)");
    CheckSame("jst.keys(jst.env('m', default={}))", kConfigs);
    CheckSame("jst.json_encode(jst.env('m'))", kConfigs);
    CheckSame(
        "[jst.file('a'), jst.symlink('b'), jst.tree('c'), jst.glob('*')]");
    CheckSame("[jst.all([]), jst.all([1, jst.env('x')])]", kConfigs);
    CheckSame("[jst.any([]), jst.any([0, jst.env('x')])]", kConfigs);
    CheckSame("[jst.sum([1, 2]), jst.prod([2, jst.env('n', default=1)])]",
              kConfigs);
    CheckSame("[jst.eq(jst.env('x'), 'foo'), jst.not(jst.env('x'))]", kConfigs);
    CheckSame("jst.foreach(function(x) [x], jst.env('l', default=[]))",
              kConfigs);
    CheckSame("jst.zip_with(function(a, b) a + b, ['a', 'b'], ['1', '2'])");
    CheckSame("jst.zip_map(['a', 'b'], jst.env('l', default=[]))", kConfigs);
    CheckSame("jst.foldl(function(x, acc) acc + [x], [], ['a', 'b'])");
    CheckSame(
        "[jst.nub_left(jst.env('l', default=[])),"
        " jst.nub_right(jst.env('l', default=[]))]",
        kConfigs);
    CheckSame("[jst.range(jst.env('n', default=2)), jst.reverse(['a', 'b'])]",
              kConfigs);
    CheckSame("jst.length(jst.env('l', default=[]))", kConfigs);
    CheckSame("jst.basename('foo/bar.txt')");
    CheckSame("jst.join_cmd(['echo', jst.env('x', default='')])", kConfigs);
    CheckSame("jst.change_ending('foo/bar.c', ending='.o')");
    CheckSame("jst.escape_chars('a-b', chars='-', prefix='+')");
    CheckSame("jst.enumerate(jst.env('l', default=[]))", kConfigs);
    CheckSame("jst.to_subdir({a: 'A'}, subdir='sub')");
    CheckSame("jst.to_subdir({'x/a': 'A'}, subdir='sub', flat=true)");
    CheckSame("jst.from_subdir({'sub/a': 'A', b: 'B'}, subdir='sub')");
    CheckSame("jst.keys({b: 1, a: 2})");
    CheckSame("jst.values(jst.env('m', default={b: 1, a: 2}))", kConfigs);
    CheckSame("jst.assert_non_empty(jst.env('l', default=['d']), msg='empty')",
              kConfigs);
    CheckSame("jst.context(jst.env('x', default='d'), 'context')", kConfigs);
    CheckSame(
        "jst.union([{a: 'A'}, jst.env('m', default={})], disjoint=true,"
        " msg=['conflict', jst.env('x')])",
        kConfigs);
    CheckSame(
        "jst.to_subdir({a: 'A'}, subdir=jst.env('x', default='d'),"
        " msg=['conflict', jst.env('x')])",
        kConfigs);
}

TEST_CASE("runtime evaluator", "[imports]") {
    auto files = file_map_t{
        {"lib.jst", "local helper(x) = [x]; {f(x): helper(x), v: 'V'}"},
        {"sub/data.jst", "local v = import '../lib.jst'; v.f('sub')"}};
    auto check = [&files](std::string const& code,
                          nlohmann::json const& expected) {
        auto config = ToConfig(nlohmann::json::object());
        auto result = EvalRuntime(Parse(code, files), &config);
        REQUIRE(result);
        CHECK(*result == expected);
    };
    check("local lib = import 'lib.jst'; [lib.f('a'), lib.v]",
          nlohmann::json::parse(R"([["a"], "V"])"));
    check("local helper = 'shadowed'; (import 'lib.jst').f('a')",
          nlohmann::json::parse(R"(["a"])"));
    check("import 'sub/data.jst'", nlohmann::json::parse(R"(["sub"])"));
}

TEST_CASE("runtime evaluator", "[deferred imports]") {
    // files parsed with deferred imports, resolved via the bindings' imports
    auto parse = [](std::string const& path, std::string const& code) {
        auto parser = jstlang::Parser{
            Context(),
            [](auto const&, auto const&, auto const*) { return std::nullopt; },
            /*defer_imports=*/true};
        return parser.ParseData(jstlang::FileData{
            .location = jstlang::FileLocation{.repo = "", .path = path},
            .content = code});
    };
    auto file = [](std::string const& path) {
        return jstlang::FileLocation{.repo = "", .path = path};
    };
    auto files = std::unordered_map<jstlang::FileLocation, jstlang::ASTNodePtr>{
        {file("lib.jst"),
         parse("lib.jst", "local helper(x) = [x]; {f(x): helper(x), v: 'V'}")},
        {file("sub/data.jst"),
         parse("sub/data.jst", "local v = import '../lib.jst'; v.f('sub')")}};
    // resolver of the loaded files, nullptr for files that are not loaded
    auto bindings = std::make_shared<jstlang::Bindings const>(
        jstlang::Bindings::bindings_t{},
        [files = std::move(files)](jstlang::FileLocation const& location) {
            auto it = files.find(location);
            return it != files.end() ? it->second : nullptr;
        });
    auto eval = [&bindings, &parse](std::string const& code,
                                    std::string* error = nullptr) {
        auto config = ToConfig(nlohmann::json::object());
        auto result = JstlangEvaluator::Evaluate(parse("TARGETS", code),
                                                 bindings,
                                                 &config,
                                                 FunctionMap::Ptr{nullptr},
                                                 [error](auto const& msg) {
                                                     if (error != nullptr) {
                                                         *error = msg;
                                                     }
                                                 });
        return result ? std::optional{result->ToJson()} : std::nullopt;
    };

    CHECK(eval("local lib = import 'lib.jst'; [lib.f('a'), lib.v]") ==
          nlohmann::json::parse(R"([["a"], "V"])"));
    CHECK(eval("import '/sub/data.jst'") ==
          nlohmann::json::parse(R"(["sub"])"));

    // not loaded, and not resolvable without imports
    auto error = std::string{};
    CHECK_FALSE(eval("import 'other.jst'", &error));
    CHECK(error.find("other.jst") != std::string::npos);
    auto result =
        JstlangEvaluator::Evaluate(parse("TARGETS", "import 'lib.jst'"),
                                   jstlang::Bindings::Empty(),
                                   nullptr,
                                   FunctionMap::Ptr{nullptr},
                                   [](auto const&) {});
    CHECK_FALSE(result);
}

TEST_CASE("runtime evaluator", "[lazy evaluation]") {
    // unused bindings and arguments are not evaluated
    CHECK(EvalRuntime("local x = jst.fail('unused'); 'ok'") == "ok");
    CHECK(EvalRuntime("local f(x) = 'ok'; f(jst.fail('unused'))") == "ok");
    // defaults are only evaluated if selected
    CHECK(EvalRuntime("jst.get('a', {a: 'A'}, default=jst.fail('unused'))") ==
          "A");
    CHECK(EvalRuntime("jst.at(0, ['A'], default=jst.fail('unused'))") == "A");
    CHECK(EvalRuntime("jst.env('x', default=jst.fail('unused'))",
                      {{"x", "X"}}) == "X");
    // short-circuit evaluation
    CHECK(EvalRuntime("false && jst.fail('unused')") == false);
    CHECK(EvalRuntime("jst.any([true, jst.fail('unused')])") == true);
    // fields of maps are only evaluated if used
    CHECK(EvalRuntime("{a: 'A', b: jst.fail('unused')}.a") == "A");
}

TEST_CASE("runtime evaluator", "[runtime types]") {
    // types are known at runtime, no need for explicit built-in functions
    CHECK(EvalRuntime("jst.env('a') + jst.env('b')",
                      {{"a", {"x"}}, {"b", {"y"}}}) ==
          nlohmann::json::parse(R"(["x", "y"])"));
    CHECK(EvalRuntime("jst.env('a') + jst.env('b')",
                      {{"a", "x"}, {"b", "y"}}) == "xy");
    CHECK(not EvalRuntime("jst.env('a') + jst.env('b')",
                          {{"a", "x"}, {"b", {"y"}}}));
    // lookups in runtime values
    CHECK(EvalRuntime("jst.env('m').k", {{"m", {{"k", "v"}}}}) == "v");
    CHECK(EvalRuntime("jst.env('m').k", {{"m", nlohmann::json::object()}}) ==
          nullptr);
    CHECK(EvalRuntime("jst.env('l')[-1]", {{"l", {"a", "b"}}}) == "b");
    // keys are sorted, as for the runtime evaluation of the preprocessor
    CHECK(EvalRuntime("jst.keys({b: 1, a: 2})") ==
          nlohmann::json::parse(R"(["a", "b"])"));
}

TEST_CASE("runtime evaluator", "[errors]") {
    auto error = std::string{};
    CHECK(not EvalRuntime("jst.fail('my message')", {}, &error));
    CHECK(error.find("my message") != std::string::npos);

    CHECK(not EvalRuntime("unknown", {}, &error));
    CHECK(error.find("Cannot find function or variable 'unknown'") !=
          std::string::npos);

    // error location points to jstlang source
    CHECK(not EvalRuntime("local f(x) = jst.join(x);\nf(42)", {}, &error));
    CHECK(error.find("<stdin>:2:") != std::string::npos);

    CHECK(not EvalRuntime("local f(x) = x; f('a', y='b')"));
    CHECK(not EvalRuntime("local f(x) = x; f('a', x='b')"));
    CHECK(not EvalRuntime("local f(x) = x; f()"));
    CHECK(not EvalRuntime("'a'('b')"));
    CHECK(not EvalRuntime("[function(x) x]"));
    // missing fields are null, like for lowering
    CheckSame("{a: 'A'}.b");

    // static evaluation does not allow reading runtime variables
    CHECK(not EvalRuntime(Parse("jst.env('x')"), nullptr, &error));
    CHECK(error.find("cannot be read") != std::string::npos);
    CHECK(EvalRuntime(Parse("local x = jst.env('x'); 'ok'"), nullptr) == "ok");
}

TEST_CASE("runtime evaluator", "[rule functions]") {
    // fake rule functions: FIELD returns a target name for "deps" and
    // ["field:<name>"] otherwise, DEP_PROVIDES returns a list for provider
    // "list" of a target name, the default otherwise
    auto functions = FunctionMap::MakePtr(
        {{"FIELD",
          [](auto&& eval, auto const& expr, auto const& env) {
              auto name = eval(expr["name"], env);
              if (name->String() == "deps") {
                  return ExpressionPtr{Expression::list_t{
                      ExpressionPtr{BuildMaps::Base::EntityName{"", "", "d"}}}};
              }
              return ExpressionPtr{
                  Expression::list_t{ExpressionPtr{"field:" + name->String()}}};
          }},
         {"DEP_PROVIDES", [](auto&& eval, auto const& expr, auto const& env) {
              auto dep = eval(expr["dep"], env);
              auto provider = eval(expr["provider"], env);
              if (not dep->IsName()) {
                  return ExpressionPtr{nullptr};
              }
              if (provider->String() == "list") {
                  return ExpressionPtr{
                      Expression::list_t{ExpressionPtr{std::string{"x"}},
                                         ExpressionPtr{Expression::kEmptyMap}}};
              }
              return eval(expr->Get("default", Expression::none_t{}), env);
          }}});
    auto eval =
        [&functions](std::string const& code) -> std::optional<nlohmann::json> {
        auto config = ToConfig(nlohmann::json::object());
        auto error = std::string{};
        auto result = JstlangEvaluator::Evaluate(
            Parse(code),
            jstlang::Bindings::Empty(),
            &config,
            functions,
            [&error](auto const& msg) { error = msg; });
        UNSCOPED_INFO("error: " << error);
        if (result) {
            return result->ToJson();
        }
        return std::nullopt;
    };

    CHECK(eval("jst.field('srcs') + ['x']") ==
          nlohmann::json::parse(R"(["field:srcs", "x"])"));
    CHECK(eval("local name = 'hdrs'; jst.field(name=name)") ==
          nlohmann::json::parse(R"(["field:hdrs"])"));
    // values of the build engine (e.g., target names) are passed through
    CHECK(eval("jst.length(jst.field('deps'))") == nlohmann::json(1));
    // default is only selected if provider is missing
    CHECK(eval("jst.dep_provides(jst.field('deps')[0], 'p', default=['d'])") ==
          nlohmann::json::parse(R"(["d"])"));
    CHECK(eval("jst.length(jst.dep_provides(jst.field('deps')[0], 'list', "
               "default=jst.fail('unused')))") == nlohmann::json(2));
    // without rule functions, rule-only functions cannot be evaluated
    auto config = ToConfig(nlohmann::json::object());
    CHECK_FALSE(JstlangEvaluator::Evaluate(Parse("jst.field('x')"),
                                           jstlang::Bindings::Empty(),
                                           &config,
                                           FunctionMap::Ptr{nullptr},
                                           [](auto const&) {}));
}
