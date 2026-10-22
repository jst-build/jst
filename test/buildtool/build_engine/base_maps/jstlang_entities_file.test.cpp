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

#include "src/buildtool/build_engine/base_maps/jstlang_entities_file.hpp"

#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/parser.hpp"

namespace {

using BuildMaps::Base::JstlangEntitiesFile;

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

[[nodiscard]] auto Parse(std::string const& code) -> jstlang::ASTNodePtr {
    auto parser = jstlang::Parser{
        Context(),
        [](auto const&, auto const&, auto const*) { return std::nullopt; }};
    return parser.ParseData(jstlang::FileData{
        .location = jstlang::FileLocation{.repo = "", .path = "<stdin>"},
        .content = code});
}

[[nodiscard]] auto ToConfig(nlohmann::json const& json) -> Configuration {
    return Configuration{Expression::FromJson(json)};
}

}  // namespace

TEST_CASE("jstlang entities file", "[entities]") {
    auto const* code = R"(
      local flag = jst.env('FLAG');
      local mk(name) = {
        type: 'generic',
        cmds: ['echo ' + name],
        flags: if flag then ['-g'] else [],
      };
      {
        b: mk('b'),
        a: {type: 'export', target: 'b', flexible_config: jst.keys({FLAG: 1})},
        c: if true then {type: 'install'} else jst.fail('unreachable'),
        d: if flag then {type: 'install'},
        e: 'not a map',
      }
    )";
    auto logger = [](auto const& msg) { INFO(msg); };
    auto file = JstlangEntitiesFile::Create(Parse(code), logger);
    REQUIRE(file);
    CHECK(file->Names() == std::vector<std::string>{"a", "b", "c", "d", "e"});
    CHECK(file->Contains("a"));
    CHECK_FALSE(file->Contains("f"));

    auto eval = [&logger](JstlangEvaluator::FieldPtr const& field,
                          std::optional<nlohmann::json> const& config)
        -> std::optional<nlohmann::json> {
        auto conf = config ? std::optional{ToConfig(*config)} : std::nullopt;
        auto result = JstlangEvaluator::Evaluate(
            field, conf ? &*conf : nullptr, FunctionMap::Ptr{nullptr}, logger);
        if (result) {
            return result->ToJson();
        }
        return std::nullopt;
    };

    SECTION("target created by function") {
        auto fields = file->GetFields("b", logger);
        REQUIRE(fields);
        CHECK(fields->size() == 3);
        // static fields
        CHECK(eval(fields->at("type"), std::nullopt) == "generic");
        CHECK(eval(fields->at("cmds"), std::nullopt) ==
              nlohmann::json::parse(R"(["echo b"])"));
        // config-dependent fields
        CHECK(eval(fields->at("flags"), nlohmann::json{{"FLAG", true}}) ==
              nlohmann::json::parse(R"(["-g"])"));
        CHECK(eval(fields->at("flags"), nlohmann::json::object()) ==
              nlohmann::json::array());
        CHECK_FALSE(eval(fields->at("flags"), std::nullopt));
    }

    SECTION("literal target") {
        auto fields = file->GetFields("a", logger);
        REQUIRE(fields);
        CHECK(eval(fields->at("flexible_config"), std::nullopt) ==
              nlohmann::json::parse(R"(["FLAG"])"));
    }

    SECTION("statically selected target") {
        auto fields = file->GetFields("c", logger);
        REQUIRE(fields);
        CHECK(eval(fields->at("type"), std::nullopt) == "install");
    }

    SECTION("invalid targets") {
        // structure must not depend on the configuration
        CHECK_FALSE(file->GetFields("d", logger));
        // target must be a map
        CHECK_FALSE(file->GetFields("e", logger));
        // target must exist
        CHECK_FALSE(file->GetFields("f", logger));
    }

    SECTION("functions selected at runtime in unfolded targets") {
        auto handlers = JstlangEntitiesFile::Create(Parse(R"(
          local handlers = {debug: function(x) x + ' -g', release: function(x) x};
          local mk(name) = {
            type: 'generic',
            outs: [name],
            cmds: [jst.get(jst.env('MODE', default='release'), handlers)(
              'touch ' + name)],
          };
          { t: mk('t') }
        )"),
                                                    logger);
        REQUIRE(handlers);
        auto fields = handlers->GetFields("t", logger);
        REQUIRE(fields);
        // the unfolded field is self-contained, evaluated directly
        REQUIRE(fields->at("cmds")->StaticValue());
        CHECK(eval(fields->at("cmds"), nlohmann::json{{"MODE", "debug"}}) ==
              nlohmann::json::parse(R"(["touch t -g"])"));
        CHECK(eval(fields->at("cmds"), nlohmann::json::object()) ==
              nlohmann::json::parse(R"(["touch t"])"));
    }

    SECTION("invalid files") {
        CHECK_FALSE(
            JstlangEntitiesFile::Create(Parse("local x = 1; [x]"), logger));
        CHECK_FALSE(
            JstlangEntitiesFile::Create(Parse("{[jst.env('x')]: {}}"), logger));
    }
}
