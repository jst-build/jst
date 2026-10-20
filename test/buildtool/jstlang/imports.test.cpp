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

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "nlohmann/json.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
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

[[nodiscard]] auto MakeReader(file_map_t files) -> jstlang::FileData::reader_t {
    return
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
}

[[nodiscard]] auto MainFile(std::string content) -> jstlang::FileData {
    return jstlang::FileData{
        .location = jstlang::FileLocation{.repo = "", .path = "main.jst"},
        .content = std::move(content)};
}

[[nodiscard]] auto Lower(jstlang::ASTNodePtr const& ast) -> nlohmann::json {
    auto opts =
        jstlang::EvalOptions{.mode = jstlang::EvalOptions::Mode::Lowering};
    return jstlang::ASTToJsonVisitor{}.ToJson(
        *jstlang::Evaluator{opts}.EvaluateAST(ast));
}

auto const kFiles = file_map_t{
    {"common.jst", "local c = jst.join(['a', 'b']); {c: c, f(x): x + c}"},
    {"a.jst", "local common = import 'common.jst'; common.f('a')"},
    {"b.jst", "local common = import 'common.jst'; common.f('b')"},
};

}  // namespace

TEST_CASE("imports", "[deferred imports]") {
    // imports are unresolved references
    auto reader = MakeReader(kFiles);
    auto main = MainFile("{a: import 'a.jst', b: import 'b.jst'}");
    auto ast =
        jstlang::Parser{Context(), reader, /*defer_imports=*/true}.ParseData(
            main);
    auto imports = jstlang::CollectImports(ast);
    REQUIRE(imports.size() == 2);
    CHECK(imports[0] == jstlang::FileLocation{.repo = "", .path = "a.jst"});
    CHECK(imports[1] == jstlang::FileLocation{.repo = "", .path = "b.jst"});
    CHECK_THROWS(Lower(ast));

    // eagerly parsed imports are resolved
    CHECK(jstlang::CollectImports(
              jstlang::Parser{Context(), reader}.ParseData(main))
              .empty());

    // unsupported deferred imports are parse errors
    CHECK_THROWS_AS((jstlang::Parser{Context(), reader, /*defer_imports=*/true})
                        .ParseData(MainFile("import ''")),
                    jstlang::ASTParseError);
}

TEST_CASE("imports", "[file locations]") {
    auto from = jstlang::FileLocation{
        .repo = "r", .path = "a/b/TARGETS", .root = jstlang::RootKind::Rules};
    auto ref = [&from](std::filesystem::path const& path) {
        return from.Resolve(path);
    };

    // relative to the importing file, absolute relative to the root
    CHECK(ref("x.jst") ==
          jstlang::FileLocation{.repo = "r",
                                .path = "a/b/x.jst",
                                .root = jstlang::RootKind::Rules});
    CHECK(ref("../c/./x.jst").path == "a/c/x.jst");
    CHECK(ref("/x.jst").path == "x.jst");
    CHECK(ref("/a/b/x.jst") == ref("x.jst"));

    // different repository or root, different file
    auto refs = std::unordered_set<jstlang::FileLocation>{ref("x.jst")};
    CHECK(refs.contains(ref("./x.jst")));
    auto other_root = ref("x.jst");
    other_root.root = jstlang::RootKind::Targets;
    CHECK_FALSE(refs.contains(other_root));
    from.repo = "s";
    CHECK_FALSE(refs.contains(ref("x.jst")));
    CHECK(ref("x.jst").ToString() == "[s rule_root] a/b/x.jst");
    CHECK(other_root.ToString() == "[r] a/b/x.jst");
    from.repo = "";
    CHECK(ref("x.jst").ToString() == "[\"\" rule_root] a/b/x.jst");
}
