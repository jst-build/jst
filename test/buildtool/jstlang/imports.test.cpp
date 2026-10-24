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
#include <vector>

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

/// \brief Reader of the files of the main repository, by path.
[[nodiscard]] auto MakeReader(file_map_t files) -> jstlang::FileReader {
    return [files = std::move(files)](jstlang::FileLocation const& location)
               -> std::optional<std::string> {
        if (auto it = files.find(location.path.string()); it != files.end()) {
            return it->second;
        }
        return std::nullopt;
    };
}

auto const kMain = jstlang::FileLocation{.repo = "", .path = "main.jst"};

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
    auto const* main = "{a: import 'a.jst', b: import 'b.jst'}";
    auto ast =
        jstlang::Parser{Context(), reader, /*defer_imports=*/true}.ParseData(
            kMain, main);
    auto imports = jstlang::CollectImports(ast);
    REQUIRE(imports.size() == 2);
    CHECK(imports[0] == jstlang::FileLocation{.repo = "", .path = "a.jst"});
    CHECK(imports[1] == jstlang::FileLocation{.repo = "", .path = "b.jst"});
    CHECK_THROWS(Lower(ast));

    // eagerly parsed imports are resolved
    CHECK(jstlang::CollectImports(
              jstlang::Parser{Context(), reader}.ParseData(kMain, main))
              .empty());

    // unsupported deferred imports are parse errors
    CHECK_THROWS_AS((jstlang::Parser{Context(), reader, /*defer_imports=*/true})
                        .ParseData(kMain, "import ''"),
                    jstlang::ASTParseError);
}

TEST_CASE("imports", "[other repositories]") {
    using jstlang::FileLocation;
    using jstlang::RootKind;
    // repository "main" binds the name "lib" to the repository "lib-v1"
    auto const files = std::unordered_map<FileLocation, std::string>{
        {FileLocation{
             .repo = "lib-v1", .path = "lib.jst", .root = RootKind::Rules},
         "{v: 'rules'}"},
        {FileLocation{.repo = "lib-v1", .path = "lib.jst"}, "{v: 'targets'}"},
        {FileLocation{
             .repo = "lib-v1", .path = "sub/x.jst", .root = RootKind::Rules},
         "import '../lib.jst'"},
    };
    auto reader =
        [&files](FileLocation const& location) -> std::optional<std::string> {
        if (auto it = files.find(location); it != files.end()) {
            return it->second;
        }
        return std::nullopt;
    };
    auto repos = [](std::string const& repo,
                    std::string const& name) -> std::optional<std::string> {
        if (repo == "main" and name == "lib") {
            return "lib-v1";
        }
        return std::nullopt;
    };
    auto parse = [&reader, &repos](RootKind root,
                                   std::string const& code,
                                   bool defer_imports = false) {
        return jstlang::Parser{Context(), reader, defer_imports, repos}
            .ParseData(
                FileLocation{.repo = "main", .path = "dir/RULES", .root = root},
                code);
    };

    // the file is read from the bound repository, from the root of the same
    // kind, with the path relative to that root
    CHECK(jstlang::CollectImports(
              parse(RootKind::Rules, "import @'lib//sub/x.jst'", true)) ==
          std::vector{FileLocation{
              .repo = "lib-v1", .path = "sub/x.jst", .root = RootKind::Rules}});
    CHECK(Lower(parse(RootKind::Rules,
                      "local lib = import @'lib//lib.jst'; lib.v")) == "rules");
    CHECK(Lower(parse(RootKind::Targets,
                      "local lib = import @'lib//lib.jst'; lib.v")) ==
          "targets");
    // relative imports of the imported file stay in the imported repository
    CHECK(Lower(parse(RootKind::Rules,
                      "local lib = import @'lib//sub/x.jst'; lib.v")) ==
          "rules");

    // names that are not bound, missing repositories, and imports from other
    // repositories without resolver are parse errors
    CHECK_THROWS_AS(parse(RootKind::Rules, "import @'other//lib.jst'", true),
                    jstlang::ASTParseError);
    CHECK_THROWS_AS(parse(RootKind::Rules, "import @'//lib.jst'", true),
                    jstlang::ASTParseError);
    CHECK_THROWS_AS((jstlang::Parser{Context(), reader, true})
                        .ParseData(kMain, "import @'lib//lib.jst'"),
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

    // in another repository: relative to its root of the same kind
    CHECK(from.ResolveIn("t", "c/../x.jst") ==
          jstlang::FileLocation{
              .repo = "t", .path = "x.jst", .root = jstlang::RootKind::Rules});
    CHECK(from.ResolveIn("t", "/x.jst") == from.ResolveIn("t", "x.jst"));

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
