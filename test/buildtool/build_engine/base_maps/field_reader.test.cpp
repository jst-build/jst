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

#include "src/buildtool/build_engine/base_maps/field_reader.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entities_file.hpp"
#include "src/buildtool/build_engine/base_maps/entity_definition.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/jstlang_entities_file.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/parser.hpp"

namespace {

using BuildMaps::Base::EntitiesFile;
using BuildMaps::Base::EntityName;
using BuildMaps::Base::FieldReader;
using nlohmann::literals::operator""_json;

[[nodiscard]] auto ToJson(ExpressionPtr const& expr) -> nlohmann::json {
    REQUIRE(expr);
    return expr->ToJson();
}

/// \brief Context of all ASTs created by the tests, which must outlive them.
[[nodiscard]] auto Context() -> jstlang::ASTContext* {
    static auto context = jstlang::ASTContext{};
    return &context;
}

[[nodiscard]] auto ParseTargets(std::string const& code) -> EntitiesFile {
    auto parser = jstlang::Parser{Context(),
                                  /*reader=*/nullptr};
    auto ast = parser.ParseData(
        jstlang::FileLocation{.repo = "", .path = "TARGETS"}, code);
    auto file =
        BuildMaps::Base::JstlangEntitiesFile::Create(ast, [](auto const&) {});
    REQUIRE(file);
    return EntitiesFile{file};
}

auto const kJsonTargets = nlohmann::json::parse(R"(
  { "foo":
    { "type": "generic"
    , "arguments_config": ["DEBUG"]
    , "cmds":
      { "type": "if"
      , "cond": {"type": "var", "name": "DEBUG"}
      , "then": ["debug"]
      , "else": ["release"]
      }
    , "unexpected": []
    }
  , "bar": {"type": "install"}
  })");

auto const* const kJstlangTargets = R"(
  local cmds(debug) = if debug then ['debug'] else ['release'];
  local debug = jst.env('DEBUG');
  {
    foo: {
      type: 'generic',
      arguments_config: ['DEBUG'],
      cmds: cmds(debug),
      unexpected: [],
    },
    bar: {type: 'install'},
  }
)";

}  // namespace

TEST_CASE("field_reader", "[targets]") {
    auto check = [](EntitiesFile const& file) {
        CHECK(file.IsObject());
        CHECK(file.Names() == std::vector<std::string>{"bar", "foo"});
        CHECK(file.Contains("foo"));
        CHECK_FALSE(file.Contains("baz"));
        CHECK_FALSE(file.GetEntity("baz", [](auto const&) {}));

        auto desc = file.GetEntity("foo", [](auto const&) {});
        REQUIRE(desc);

        auto messages = std::vector<std::string>{};
        auto logger = std::make_shared<AsyncMapConsumerLogger>(
            [&messages](auto const& msg, bool /*fatal*/) {
                messages.emplace_back(msg);
            });
        auto reader = FieldReader::CreatePtr(
            *desc, EntityName{"", ".", "foo"}, "target", logger);
        REQUIRE(reader);

        // static fields
        CHECK(reader->ReadStringList("arguments_config") ==
              std::vector<std::string>{"DEBUG"});
        CHECK(reader->ReadStringList("missing") == std::vector<std::string>{});
        auto type = reader->ReadExpression("type");
        REQUIRE(type);
        CHECK(type == Expression::FromJson(R"("generic")"_json));

        // evaluated fields
        auto cmds = reader->ReadFieldExpression("cmds");
        REQUIRE(cmds);
        auto debug =
            Configuration{Expression::FromJson(R"({"DEBUG": true})"_json)};
        auto release = Configuration{Expression::FromJson(R"({})"_json)};
        CHECK(ToJson(cmds.Evaluate(debug, FunctionMap::Ptr{nullptr})) ==
              R"(["debug"])"_json);
        CHECK(ToJson(cmds.Evaluate(release, FunctionMap::Ptr{nullptr})) ==
              R"(["release"])"_json);
        auto deps =
            reader->ReadOptionalFieldExpression("deps", Expression::kEmptyList);
        REQUIRE(deps);
        CHECK(ToJson(deps.Evaluate(release, FunctionMap::Ptr{nullptr})) ==
              nlohmann::json::array());

        // missing mandatory field
        messages.clear();
        CHECK_FALSE(reader->ReadFieldExpression("missing"));
        CHECK_FALSE(messages.empty());

        // unexpected fields are reported
        messages.clear();
        reader->ExpectFields({"type", "arguments_config", "cmds"});
        REQUIRE(messages.size() == 1);
        CHECK(messages[0].find("unexpected") != std::string::npos);
    };

    SECTION("json") {
        check(EntitiesFile{kJsonTargets});
    }

    SECTION("jstlang") {
        auto file = ParseTargets(kJstlangTargets);
        check(file);

        // config-dependent fields cannot be read as plain JSON
        auto desc = file.GetEntity("foo", [](auto const&) {});
        REQUIRE(desc);
        auto failed = false;
        auto logger = std::make_shared<AsyncMapConsumerLogger>(
            [&failed](auto const& /*msg*/, bool fatal) { failed |= fatal; });
        auto reader = FieldReader::CreatePtr(
            *desc, EntityName{"", ".", "foo"}, "target", logger);
        REQUIRE(reader);
        CHECK_FALSE(reader->ReadStringList("cmds"));
        CHECK(failed);

        // display only contains statically evaluable fields
        CHECK(desc->ToJson() == R"(
          { "type": "generic"
          , "arguments_config": ["DEBUG"]
          , "unexpected": []
          })"_json);
    }
}
