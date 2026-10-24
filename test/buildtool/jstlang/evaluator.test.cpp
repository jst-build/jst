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

#include "src/buildtool/jstlang/evaluator.hpp"

#include <cstddef>
#include <memory>
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
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/parser.hpp"

namespace {

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

/// \brief Parse code, optionally with deferred imports.
[[nodiscard]] auto Parse(std::string const& code,
                         bool defer_imports = false) -> jstlang::ASTNodePtr {
    auto parser = jstlang::Parser{Context(), /*reader=*/nullptr, defer_imports};
    return parser.ParseData(
        jstlang::FileLocation{.repo = "", .path = "<stdin>"}, code);
}

/// \brief Literal AST of a JSON value.
[[nodiscard]] auto FromJson(nlohmann::json const& json) -> jstlang::ASTNodePtr {
    auto loc = jstlang::Location{};
    if (json.is_null()) {
        return std::make_shared<jstlang::NullNode>(loc);
    }
    if (json.is_boolean()) {
        return std::make_shared<jstlang::BoolNode>(loc, json.get<bool>());
    }
    if (json.is_number()) {
        return std::make_shared<jstlang::NumberNode>(loc, json.get<double>());
    }
    if (json.is_string()) {
        return std::make_shared<jstlang::StringNode>(loc,
                                                     json.get<std::string>());
    }
    if (json.is_array()) {
        auto items = jstlang::ListNode::items_t{};
        for (auto const& item : json) {
            items.emplace_back(FromJson(item));
        }
        return std::make_shared<jstlang::ListNode>(loc, std::move(items));
    }
    auto fields = jstlang::MapNode::fields_t{};
    for (auto const& [key, value] : json.items()) {
        fields.emplace_back(std::make_shared<jstlang::StringNode>(loc, key),
                            FromJson(value));
    }
    return std::make_shared<jstlang::MapNode>(loc, std::move(fields));
}

[[nodiscard]] auto ToJson(jstlang::ASTNodePtr const& value) -> nlohmann::json {
    return jstlang::ASTToJsonVisitor{jstlang::VerbatimType::Full,
                                     /*json_only=*/true}
        .ToJson(*value);
}

/// \brief Full evaluation (mode 1), nullopt on error.
[[nodiscard]] auto EvalFull(jstlang::ASTNodePtr const& ast,
                            nlohmann::json const& config)
    -> std::optional<nlohmann::json> {
    auto lookup = jstlang::EvalOptions::ConfigLookup{
        [&config](std::string const& name) -> jstlang::ASTNodePtr {
            if (config.contains(name)) {
                return FromJson(config.at(name));
            }
            return nullptr;
        }};
    auto options = jstlang::EvalOptions{
        .mode = jstlang::EvalOptions::Mode::Full, .config = &lookup};
    try {
        auto value = jstlang::Evaluator{options}.EvaluateAST(ast);
        jstlang::RequireValue(value);
        return ToJson(value);
    } catch (...) {
        return std::nullopt;
    }
}

/// \brief Unfolding (without configuration).
[[nodiscard]] auto Unfold(
    jstlang::ASTNodePtr const& ast,
    std::size_t unroll_limit = jstlang::EvalOptions::kDefaultUnrollLimit)
    -> jstlang::ASTNodePtr {
    return jstlang::Evaluator{
        jstlang::EvalOptions{.mode = jstlang::EvalOptions::Mode::Unfold,
                             .unroll_limit = unroll_limit}}
        .EvaluateAST(ast);
}

/// \brief Lowering to the expression language.
[[nodiscard]] auto Lower(
    jstlang::ASTNodePtr const& ast,
    std::size_t unroll_limit = jstlang::EvalOptions::kDefaultUnrollLimit)
    -> nlohmann::json {
    auto lowered =
        jstlang::Evaluator{
            jstlang::EvalOptions{.mode = jstlang::EvalOptions::Mode::Lowering,
                                 .unroll_limit = unroll_limit}}
            .EvaluateAST(ast);
    return jstlang::ASTToJsonVisitor{}.ToJson(*lowered);
}

/// \brief Lowering to the expression language and evaluation by the
/// expression evaluator, nullopt on error.
[[nodiscard]] auto EvalLowered(
    jstlang::ASTNodePtr const& ast,
    nlohmann::json const& config,
    std::size_t unroll_limit = jstlang::EvalOptions::kDefaultUnrollLimit)
    -> std::optional<nlohmann::json> {
    try {
        auto expr = Expression::FromJson(Lower(ast, unroll_limit));
        auto result = Evaluator::EvaluateExpression(
            expr,
            Configuration{Expression::FromJson(config)},
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

/// \brief Value of the build engine for testing, identified by a string.
class TestValue final : public jstlang::OpaqueValue {
  public:
    explicit TestValue(std::string id, bool comparable = true)
        : id_{std::move(id)}, comparable_{comparable} {}
    [[nodiscard]] auto ToJson() const -> nlohmann::json final {
        return nullptr;
    }
    [[nodiscard]] auto Identity() const -> std::string final { return id_; }
    [[nodiscard]] auto IsComparable() const -> bool final {
        return comparable_;
    }
    [[nodiscard]] auto ToString() const -> std::string final { return id_; }

  private:
    std::string id_;
    bool comparable_;
};

}  // namespace

TEST_CASE("evaluate", "[consistency]") {
    // full evaluation == lowering + expression evaluator == unfolding + full
    // evaluation, for several configurations and unroll limits
    auto const programs = std::vector<std::string>{
        R"(
          local prefix = jst.env('prefix', default='p');
          local add(x, sep='-') = prefix + sep + x;
          { names: [add(x) for x in jst.env('list', default=[])],
            first: jst.at(0, jst.env('list', default=['none'])),
            joined: add('j', sep=jst.join(['+', '+'])) }
        )",
        R"(
          local m = jst.union([{a: 'a'}, jst.env('map', default={})]);
          local ks = jst.keys(m);
          { keys: ks,
            len: jst.length(ks),
            get: jst.get('a', m, default='none'),
            json: jst.json_encode(m) }
        )",
        R"(
          local c = jst.env('cond');
          local pick(x) = if c then x else 'no';
          [pick('yes'), if c then jst.env('list') else [],
           jst.foldl(function(x, acc) acc + [x, x], [], jst.env('list',
                     default=['f']))]
        )",
        R"(
          local base = {k: 'v'};
          local f(x) = jst.to_subdir({[x]: 'file'}, subdir='d');
          local g = jst.zip_with(function(a, b) jst.join([a, b]), ['x', 'y'],
                                 jst.env('list', default=['1', '2']));
          [f(jst.env('prefix', default='name')), g, jst.get('k', base),
           jst.eq(jst.env('map', default={}), {}), jst.sum([1, 2, 3])]
        )",
        R"(
          local c = jst.env('cond');
          [['a'] + (if c then ['b'] else @'//dir:name'),
           [@':local', @'./rel:t', @'repo//mod:t'] + [@'//x:y']]
        )",
        R"(
          local m = jst.env('map', default=false);
          [m && jst.get('a', m), !m || jst.get('a', m),
           jst.all([m, jst.get('a', m)]), jst.any([!m, jst.get('a', m)])]
        )",
    };
    // not an initializer list, which triggers false positives of GCC 15
    // (free-nonheap-object)
    auto configs = std::vector<nlohmann::json>{};
    configs.emplace_back(nlohmann::json::object());
    configs.emplace_back(
        nlohmann::json::parse(R"({"prefix": "q", "list": ["a", "b"],
                                   "cond": true, "map": {"b": "x"}})"));
    configs.emplace_back(nlohmann::json::parse(R"({"list": ["c"], "cond": false,
                                   "map": {"a": "y"}})"));

    for (auto const& program : programs) {
        auto ast = Parse(program);
        auto unfolded = Unfold(ast);
        auto unfolded_no_unroll = Unfold(ast, /*unroll_limit=*/0);
        for (auto const& config : configs) {
            INFO("program: " << program << "\nconfig: " << config.dump());
            auto full = EvalFull(ast, config);
            CHECK(full == EvalLowered(ast, config));
            CHECK(full == EvalLowered(ast, config, /*unroll_limit=*/0));
            CHECK(full == EvalFull(unfolded, config));
            CHECK(full == EvalFull(unfolded_no_unroll, config));
        }
    }
}

TEST_CASE("evaluate", "[full evaluation]") {
    auto full = jstlang::EvalOptions{.mode = jstlang::EvalOptions::Mode::Full,
                                     .config = nullptr};

    SECTION("static evaluation of runtime variables is an error") {
        auto evaluator = jstlang::Evaluator{full};
        CHECK_THROWS_AS(evaluator.EvaluateAST(Parse("jst.env('x')")),
                        jstlang::ASTEvalError);
        // but only if read
        CHECK(ToJson(evaluator.EvaluateAST(Parse(
                  "local x = jst.env('x'); 'y'"))) == nlohmann::json("y"));
    }

    SECTION("unset variables are null or their default") {
        CHECK(EvalFull(Parse("[jst.env('x'), jst.env('y', default=1)]"),
                       nlohmann::json::parse(R"({"y": null})")) ==
              nlohmann::json::parse("[null, 1]"));
    }

    SECTION("failure only if needed") {
        auto ast = Parse(
            "if jst.env('c') then jst.fail('boom') else "
            "jst.get('a', {a: 1}, default=jst.fail('unused'))");
        CHECK(EvalFull(ast, nlohmann::json::parse(R"({"c": false})")) ==
              nlohmann::json(1));
        CHECK_FALSE(EvalFull(ast, nlohmann::json::parse(R"({"c": true})")));
    }

    SECTION("&&, ||, all() and any() use short-circuit evaluation") {
        // the right operands look up a field in a map, an error for a bool
        auto const no_map = nlohmann::json::parse(R"({"m": false})");
        auto const map = nlohmann::json::parse(R"({"m": {"a": "x"}})");
        auto const get = std::string{"jst.get('a', jst.env('m'))"};
        auto const programs = std::vector<std::pair<std::string, bool>>{
            {"jst.env('m') && " + get, false},
            {"!jst.env('m') || " + get, true},
            {"jst.all([jst.env('m'), " + get + "])", false},
            {"jst.any([!jst.env('m'), " + get + "])", true},
        };
        for (auto const& [program, result] : programs) {
            INFO("program: " << program);
            auto ast = Parse(program);
            CHECK(EvalFull(ast, no_map) == nlohmann::json(result));
            // operands not deciding the result are evaluated
            CHECK(EvalFull(ast, map) == nlohmann::json(true));
            CHECK_FALSE(
                EvalFull(ast, nlohmann::json::parse(R"({"m": "no map"})")));
        }
    }
}

TEST_CASE("evaluate", "[unfolding]") {
    auto const config = nlohmann::json::parse(
        R"({"r1": ["a", "b"], "r2": ["c"], "foo": "f", "range": ["x", "y"]})");

    SECTION("locals and functions are unfolded, unknowns stay jstlang") {
        auto ast = Parse(R"(
          local x = jst.join(['a', 'b']);
          local f(y) = if y then x else jst.join(jst.env('r1'));
          [f(true), f(jst.env('c'))]
          )");
        auto unfolded = Unfold(ast);
        // no locals or calls of functions remain, the condition and the
        // call of the built-in function with unknown arguments stay
        auto const* list =
            jstlang::ASTNode::Cast<jstlang::ListNode const*>(unfolded.get());
        REQUIRE(list != nullptr);
        REQUIRE(list->GetItems().size() == 2);
        CHECK(list->GetItems()[0]->Kind() == jstlang::NodeKind::String);
        auto const* cond = jstlang::ASTNode::Cast<jstlang::IfNode const*>(
            list->GetItems()[1].get());
        REQUIRE(cond != nullptr);
        CHECK(cond->GetElseBranch()->Kind() == jstlang::NodeKind::Call);
        CHECK(EvalFull(unfolded, nlohmann::json{{"c", false}, {"r1", {"p"}}}) ==
              nlohmann::json::parse(R"(["ab", "p"])"));
    }

    SECTION("iteration variables are not captured") {
        auto ast = Parse(R"(
          local f(v) = [v + x for x in jst.env('r2')];
          [f(x) for x in jst.env('r1')]
          )");
        CHECK(EvalFull(Unfold(ast), config) == EvalFull(ast, config));
        CHECK(EvalFull(ast, config) ==
              nlohmann::json::parse(R"([["ac"], ["bc"]])"));
    }

    SECTION("calls of unknown functions stay calls") {
        // function selected by a runtime variable
        auto ast = Parse(R"(
          local handlers = {debug: function(x) x + ' -g', release: function(x) x};
          jst.get(jst.env('MODE', default='release'), handlers)('cc')
          )");
        auto unfolded = Unfold(ast);
        CHECK(unfolded->Kind() == jstlang::NodeKind::Call);
        CHECK(EvalFull(ast, nlohmann::json{{"MODE", "debug"}}) == "cc -g");
        CHECK(EvalFull(ast, nlohmann::json::object()) == "cc");
        // the closures of the map are unfolded to self-contained functions
        CHECK(jstlang::IsSelfContained(*unfolded));
        CHECK(EvalFull(unfolded, nlohmann::json{{"MODE", "debug"}}) == "cc -g");
        CHECK(EvalFull(unfolded, nlohmann::json::object()) == "cc");
    }

    SECTION("closures in residual code are unfolded") {
        auto const programs = std::vector<std::string>{
            // closures in data
            R"(
              local handlers = {debug: function(x) x, release: function(x) x};
              jst.keys(jst.union([handlers, {[jst.env('N', default='c')]: 1}]))
            )",
            // closures selected by a condition, named arguments and defaults
            R"(
              local f = if jst.env('C') then function(a, b=a + '!') a + b
                        else function(a, b='?') b + a;
              [f('x'), f(a='y', b='z'), f(b='w', a='v')]
            )",
            // parameters of the same name are not captured
            R"(
              local outer(x) = local h(y) = x + y; function(x) h(x);
              local g = if jst.env('C') then outer('a') else outer('b');
              g('c')
            )",
            // higher-order functions
            R"(
              local apply(f, v) = f(v);
              local fs = {a: function(x) x + '1', b: function(x) x + '2'};
              local g = jst.get(jst.env('K', default='a'), fs);
              [apply(g, 'p'), apply(function(y) g(y + '-'), 'q')]
            )",
        };
        auto configs = std::vector<nlohmann::json>{};
        configs.emplace_back(nlohmann::json::object());
        configs.emplace_back(nlohmann::json{{"C", true}, {"K", "b"}});
        configs.emplace_back(nlohmann::json{{"C", "d"}, {"N", "n"}});
        for (auto const& program : programs) {
            auto ast = Parse(program);
            auto unfolded = Unfold(ast);
            CHECK(jstlang::IsSelfContained(*unfolded));
            for (auto const& config : configs) {
                INFO("program: " << program << "\nconfig: " << config.dump());
                auto full = EvalFull(ast, config);
                CHECK(full);
                CHECK(full == EvalFull(unfolded, config));
            }
        }
        CHECK(EvalFull(Parse(programs[2]), nlohmann::json{{"C", true}}) ==
              "ac");
        CHECK(EvalFull(Parse(programs[1]), nlohmann::json{{"C", true}}) ==
              nlohmann::json::parse(R"(["xx!", "yz", "vw"])"));
    }

    SECTION("iteration variables do not shadow runtime variables") {
        auto ast = Parse("[foo + jst.env('foo') for foo in jst.env('range')]");
        CHECK_THROWS(Lower(ast));
        CHECK(EvalFull(Unfold(ast), config) ==
              nlohmann::json::parse(R"(["xf", "yf"])"));
    }
}

TEST_CASE("evaluate", "[unroll limit]") {
    auto const config = nlohmann::json::object();
    auto const programs = std::vector<std::string>{
        "[x + '!' for x in ['a', 'b']]",
        "jst.foldl(function(x, acc) jst.join([acc, x]), '', ['c', 'd'])",
        "jst.zip_with(function(x, y) jst.join([x, y]), ['e', 'f'], "
        "['g', 'h'])",
    };
    for (auto const& program : programs) {
        INFO("program: " << program);
        auto ast = Parse(program);

        // lowering: loops are unrolled (evaluated statically) up to the
        // limit, larger ones stay loops of the expression language
        auto unrolled = Lower(ast);
        CHECK(unrolled == EvalFull(ast, config));  // a literal
        CHECK(Lower(ast, /*unroll_limit=*/1) != unrolled);
        CHECK(EvalLowered(ast, config, /*unroll_limit=*/1) ==
              EvalFull(ast, config));
    }
}

TEST_CASE("evaluate", "[bindings]") {
    auto const full =
        jstlang::EvalOptions{.mode = jstlang::EvalOptions::Mode::Full};
    auto eval = [&full](std::string const& code,
                        jstlang::Bindings::Ptr const& bindings,
                        bool defer_imports = false) {
        return jstlang::ValueToJson(
            *jstlang::Evaluator{full, bindings}.EvaluateAST(
                Parse(code, defer_imports)));
    };

    // only the built-ins object
    CHECK(eval("jst.join(['a', 'b'])", jstlang::Bindings::Empty()) == "ab");

    // top-level locals, evaluated lazily in the scope of the ones before
    auto const bindings =
        std::make_shared<jstlang::Bindings const>(jstlang::Bindings::bindings_t{
            {"a", Parse("jst.join(['x', 'y'])")},
            {"b", Parse("a + '!'")},
            {"unused", Parse("jst.fail('not evaluated')")}});
    CHECK(eval("[a, b]", bindings) ==
          nlohmann::json::parse(R"(["xy", "xy!"])"));

    // values are converted as in the expression language
    CHECK(eval("@'//m:t'", jstlang::Bindings::Empty()) ==
          nlohmann::json::parse(R"(["m", "t"])"));

    // deferred imports are resolved via the resolver of the bindings
    auto const imported = Parse("'imported'");
    auto const with_imports = std::make_shared<jstlang::Bindings const>(
        jstlang::Bindings::bindings_t{},
        [&imported](jstlang::FileLocation const& file) {
            return file.path == "x.jst" ? imported : nullptr;
        });
    CHECK(eval("import 'x.jst'", with_imports, /*defer_imports=*/true) ==
          "imported");
    CHECK_THROWS_AS(
        eval("import 'y.jst'", with_imports, /*defer_imports=*/true),
        jstlang::ASTEvalError);
}

TEST_CASE("evaluate", "[reuse unchanged nodes]") {
    // literal map without the binding of the built-ins
    auto ast =
        FromJson(nlohmann::json::parse(R"({"a": ["x", "y"], "b": "z"})"));
    auto evaluate = [&ast](jstlang::EvalOptions::Mode mode) {
        auto options = jstlang::EvalOptions{.mode = mode};
        return jstlang::Evaluator{options}.EvaluateAST(ast);
    };
    using Mode = jstlang::EvalOptions::Mode;

    // unfolding and lowering reuse unchanged nodes, full evaluation rebuilds
    CHECK(evaluate(Mode::Unfold) == ast);
    CHECK(evaluate(Mode::Lowering) == ast);
    auto rebuilt = evaluate(Mode::Full);
    CHECK(rebuilt != ast);
    CHECK(ToJson(rebuilt) == ToJson(ast));
}

TEST_CASE("evaluate", "[external values]") {
    // runtime variables are values of the build engine: artifacts o1, o1_copy
    // (equal to o1), o2; result r1 (same identity as o1); entity names n1,
    // n1_copy (equal to n1, but not comparable implicitly)
    auto lookup = jstlang::EvalOptions::ConfigLookup{
        [](std::string const& name) -> jstlang::ASTNodePtr {
            auto const loc = jstlang::Location{};
            if (name == "r1") {
                return std::make_shared<jstlang::ResultNode>(
                    loc, std::make_shared<TestValue>("o1"));
            }
            if (name.starts_with("n1")) {
                return std::make_shared<jstlang::EntityNameNode>(
                    loc,
                    std::make_shared<TestValue>("n1", /*comparable=*/false));
            }
            auto id = name == "o1_copy" ? std::string{"o1"} : name;
            return std::make_shared<jstlang::ArtifactNode>(
                loc, std::make_shared<TestValue>(id));
        }};
    auto options = jstlang::EvalOptions{
        .mode = jstlang::EvalOptions::Mode::Full, .config = &lookup};
    auto eval = [&options](std::string const& code) {
        auto value = jstlang::Evaluator{options}.EvaluateAST(Parse(R"(
              local o1 = jst.env('o1');
              local o1_copy = jst.env('o1_copy');
              local o2 = jst.env('o2');
              local r1 = jst.env('r1');
              local n1 = jst.env('n1');
              local n1_copy = jst.env('n1_copy');
            )" + code));
        jstlang::RequireValue(value);
        return value;
    };

    // compared by identity, values of different types are never equal
    CHECK(ToJson(eval("[jst.eq(o1, o1_copy), jst.eq(o1, o2), o1 == 'o1', "
                      "o1 == r1, n1 == n1_copy]")) ==
          nlohmann::json::parse("[true, false, false, false, true]"));

    // deduplicated by identity, names cannot be compared implicitly
    CHECK(ToJson(eval("jst.length(jst.nub_left([o1, o2, o1_copy, r1]))")) ==
          nlohmann::json(3));
    CHECK_THROWS(eval("jst.nub_left([n1, n1_copy])"));
    CHECK(ToJson(eval("jst.keys(jst.to_subdir({'a/x': o1, 'b/x': o1_copy, "
                      "y: o2}, subdir='d', flat=true))")) ==
          nlohmann::json::parse(R"(["d/x", "d/y"])"));
    CHECK_THROWS(eval("jst.to_subdir({'a/x': o1, 'b/x': o2}, flat=true)"));

    // encoded as their JSON value (null if not JSON)
    CHECK(ToJson(eval("jst.json_encode([o1])")) == nlohmann::json("[null]"));

    // typed: operations of other types are rejected
    CHECK_THROWS(eval("o1 + 'x'"));
    CHECK_THROWS(eval("jst.values(o1)"));
    CHECK_THROWS(eval("jst.assert_non_empty(o1)"));
    CHECK(ToJson(eval("[jst.length(jst.values({a: o1, b: r1})), "
                      "jst.assert_non_empty([o1]) == [o1_copy], "
                      "jst.context(o1, msg='ctx') == o1]")) ==
          nlohmann::json::parse("[2, true, true]"));

    // passed through, keeping their type
    auto value = eval("jst.get('k', {k: o2})");
    auto const* artifact =
        jstlang::ASTNode::Cast<jstlang::ArtifactNode const*>(value.get());
    REQUIRE(artifact != nullptr);
    CHECK(artifact->EvalType() == jstlang::ValueType::Artifact);
    CHECK(artifact->GetValue()->Identity() == "o2");
}
