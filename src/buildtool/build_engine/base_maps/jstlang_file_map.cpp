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

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/parser.hpp"

namespace BuildMaps::Base {

namespace {

/// \brief Read the content of a jstlang file.
/// \returns nullopt on error (reported via logger).
[[nodiscard]] auto ReadJstlangFile(
    gsl::not_null<RepositoryConfig const*> const& repo_config,
    jstlang::FileLocation const& file,
    AsyncMapConsumerLoggerPtr const& logger) -> std::optional<std::string> {
    auto const* root = file.root == jstlang::RootKind::Rules
                           ? repo_config->RuleRoot(file.repo)
                           : repo_config->TargetRoot(file.repo);
    if (root == nullptr) {
        (*logger)("Cannot determine root of file " + file.ToString() + ".",
                  /*fatal=*/true);
        return std::nullopt;
    }
    if (root->IsAbsent()) {
        (*logger)(
            "Would have to read file " + file.ToString() + " of absent root.",
            /*fatal=*/true);
        return std::nullopt;
    }
    if (not root->IsFile(file.path)) {
        (*logger)("File " + file.ToString() + " does not exist.",
                  /*fatal=*/true);
        return std::nullopt;
    }
    auto content = root->ReadContent(file.path);
    if (not content) {
        (*logger)("Cannot read file " + file.ToString() + ".", /*fatal=*/true);
    }
    return content;
}

/// \brief Parse the content of a jstlang file, with deferred imports, in the
/// given context (which owns the parsed file). Imports from other repositories
/// are resolved via the bindings of the file's repository.
/// \returns nullptr on error (reported via logger).
[[nodiscard]] auto ParseJstlangFile(
    gsl::not_null<RepositoryConfig const*> const& repo_config,
    gsl::not_null<jstlang::ASTContext*> const& context,
    jstlang::FileLocation const& file,
    std::string content,
    AsyncMapConsumerLoggerPtr const& logger) -> jstlang::ASTNodePtr {
    try {
        // imports are deferred, so the parser never reads other files
        auto parser = jstlang::Parser{
            context,
            /*reader=*/nullptr,
            /*defer_imports=*/true,
            [repo_config](std::string const& repo, std::string const& name)
                -> std::optional<std::string> {
                if (auto const* global = repo_config->GlobalName(repo, name)) {
                    return *global;
                }
                return std::nullopt;
            }};
        auto ast = parser.ParseData(file, std::move(content));
        if (ast) {
            return ast;
        }
        (*logger)("Parsing file " + file.ToString() + " failed.",
                  /*fatal=*/true);
    } catch (std::exception const& e) {
        (*logger)("Parsing file " + file.ToString() + " failed:\n" + e.what(),
                  /*fatal=*/true);
    }
    return nullptr;
}

/// \brief Locations of the files imported by the parsed AST of a file
/// (deduplicated, in order of appearance; resolved relative to the file).
[[nodiscard]] auto CollectFileImports(jstlang::ASTNodePtr const& ast)
    -> std::vector<jstlang::FileLocation> {
    auto imports = std::vector<jstlang::FileLocation>{};
    auto seen = std::unordered_set<jstlang::FileLocation>{};
    for (auto& imported : jstlang::CollectImports(ast)) {
        if (seen.insert(imported).second) {
            imports.emplace_back(std::move(imported));
        }
    }
    return imports;
}

/// \brief Union of the given imported files and the files they import.
[[nodiscard]] auto MergeImports(std::vector<jstlang::FileLocation> const& files,
                                std::vector<JstlangFile const*> const& values)
    -> std::shared_ptr<JstlangFile::imports_t const> {
    auto merged = JstlangFile::imports_t{};
    auto max_size = files.size();
    for (std::size_t i = 0; i < files.size(); ++i) {
        max_size += values[i]->imports->size();
    }
    merged.reserve(max_size);
    for (std::size_t i = 0; i < files.size(); ++i) {
        merged.try_emplace(files[i], values[i]->ast);
        auto const& imports = *(values[i]->imports);
        merged.insert(imports.begin(), imports.end());
    }
    return std::make_shared<JstlangFile::imports_t const>(std::move(merged));
}

}  // namespace

auto CreateJstlangFileMap(
    gsl::not_null<RepositoryConfig const*> const& repo_config,
    std::size_t jobs) -> JstlangFileMap {
    auto load_file = [repo_config](auto /*ts*/,
                                   auto setter,
                                   auto logger,
                                   auto subcaller,
                                   jstlang::FileLocation const& key) {
        auto content = ReadJstlangFile(repo_config, key, logger);
        if (not content) {
            return;
        }
        // the context owns the parsed file, it is kept in the map's value
        auto context = std::make_shared<jstlang::ASTContext>();
        auto ast = ParseJstlangFile(
            repo_config, context.get(), key, *std::move(content), logger);
        if (not ast) {
            return;
        }
        auto imports = CollectFileImports(ast);
        (*subcaller)(
            imports,
            [setter, context, ast, imports](auto const& values) {
                (*setter)(
                    JstlangFile{.context = context,
                                .ast = ast,
                                .imports = MergeImports(imports, values)});
            },
            std::make_shared<AsyncMapConsumerLogger>(
                [logger, key](auto const& msg, bool fatal) {
                    (*logger)("While importing files from " + key.ToString() +
                                  ":\n" + msg,
                              fatal);
                }));
    };
    return JstlangFileMap{load_file, jobs};
}

}  // namespace BuildMaps::Base
