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

#include "src/buildtool/build_engine/base_maps/jstlang_file_map.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "catch2/catch_test_macros.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

namespace {

using namespace BuildMaps::Base;  // NOLINT

/// \brief Repository with a rules root containing the given files.
[[nodiscard]] auto SetupRepo(
    std::string const& name,
    std::vector<std::pair<std::string, std::string>> const& files)
    -> RepositoryConfig {
    auto root = std::filesystem::absolute("jstlang_file_map_" + name);
    std::filesystem::remove_all(root);
    for (auto const& [path, content] : files) {
        std::filesystem::create_directories((root / path).parent_path());
        std::ofstream{root / path} << content;
    }
    auto info = RepositoryConfig::RepositoryInfo{FileRoot{root}};
    RepositoryConfig repo_config{};
    repo_config.SetInfo("", std::move(info));
    return repo_config;
}

[[nodiscard]] auto File(std::string const& path) -> jstlang::FileLocation {
    return jstlang::FileLocation{
        .repo = "", .path = path, .root = jstlang::RootKind::Rules};
}

struct LoadResult {
    std::optional<JstlangFile> file;
    std::vector<std::string> errors;
};

/// \brief Load file via the map, which must outlive the use of the result (it
/// owns the ASTs of the imported files).
[[nodiscard]] auto Load(JstlangFileMap* jstlang_file_map,
                        jstlang::FileLocation const& file) -> LoadResult {
    auto result = LoadResult{};
    {
        TaskSystem ts;
        jstlang_file_map->ConsumeAfterKeysReady(
            &ts,
            {file},
            [&result](auto const& values) { result.file = *values[0]; },
            [&result](std::string const& msg, bool fatal) {
                if (fatal) {
                    result.errors.emplace_back(msg);
                }
            });
    }
    if (not result.file) {
        if (auto cycle = jstlang_file_map->DetectCycle()) {
            result.errors.emplace_back("cycle");
        }
    }
    return result;
}

[[nodiscard]] auto ImportedPaths(JstlangFile const& file)
    -> std::set<std::string> {
    auto paths = std::set<std::string>{};
    for (auto const& [ref, ast] : *file.imports) {
        CHECK(ast != nullptr);
        paths.emplace(ref.path.string());
    }
    return paths;
}

}  // namespace

TEST_CASE("jstlang file map", "[transitive imports]") {
    auto repo_config =
        SetupRepo("transitive",
                  {{"main.jst",
                    "local a = import 'a/a.jst'; local b = import "
                    "'/b.jst'; [a, b]"},
                   {"a/a.jst", "local c = import '../c.jst'; {c: c, x: 1 + 2}"},
                   {"b.jst", "import '/a/../c.jst'"},
                   {"c.jst", "'c'"}});
    auto jstlang_file_map = CreateJstlangFileMap(&repo_config);
    auto result = Load(&jstlang_file_map, File("main.jst"));
    for (auto const& error : result.errors) {
        UNSCOPED_INFO(error);
    }
    REQUIRE(result.errors.empty());
    REQUIRE(result.file);
    CHECK(result.file->ast != nullptr);
    CHECK(ImportedPaths(*result.file) ==
          std::set<std::string>{"a/a.jst", "b.jst", "c.jst"});

    // imports stay deferred
    auto const* let = jstlang::ASTNode::Cast<jstlang::LetNode const*>(
        result.file->imports->at(File("a/a.jst")).get());
    REQUIRE(let != nullptr);
    CHECK(let->GetValue()->Kind() == jstlang::NodeKind::Foreign);

    // a file without imports
    auto leaf = Load(&jstlang_file_map, File("c.jst"));
    REQUIRE(leaf.file);
    CHECK(leaf.file->imports->empty());
}

TEST_CASE("jstlang file map", "[errors]") {
    auto repo_config = SetupRepo("errors",
                                 {{"missing.jst", "import 'none.jst'"},
                                  {"syntax.jst", "{a: "},
                                  {"cycle_a.jst", "import 'cycle_b.jst'"},
                                  {"cycle_b.jst", "import 'cycle_a.jst'"}});
    for (auto const* path :
         {"missing.jst", "syntax.jst", "cycle_a.jst", "none.jst"}) {
        INFO(path);
        auto jstlang_file_map = CreateJstlangFileMap(&repo_config);
        auto result = Load(&jstlang_file_map, File(path));
        CHECK_FALSE(result.file);
        CHECK_FALSE(result.errors.empty());
    }
}

TEST_CASE("jstlang file map", "[loaded once]") {
    // a file requested directly and imported by another one is loaded once
    auto repo_config = SetupRepo(
        "once", {{"TARGETS", "{a: import 'lib.jst'}"}, {"lib.jst", "'lib'"}});
    auto jstlang_file_map = CreateJstlangFileMap(&repo_config);
    auto files = std::vector<JstlangFile>{};
    {
        TaskSystem ts;
        jstlang_file_map.ConsumeAfterKeysReady(
            &ts,
            {File("TARGETS"), File("lib.jst")},
            [&files](auto const& values) { files = {*values[0], *values[1]}; },
            [](auto const& /*msg*/, bool /*fatal*/) { CHECK(false); });
    }
    REQUIRE(files.size() == 2);
    CHECK(files[0].imports->at(File("lib.jst")) == files[1].ast);
}
