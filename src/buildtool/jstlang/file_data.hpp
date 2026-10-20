// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_FILE_DATA_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_FILE_DATA_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "src/buildtool/jstlang/ref.hpp"
#include "src/utils/cpp/hash_combine.hpp"
#include "src/utils/cpp/path_hash.hpp"

namespace jstlang {

/// \brief Root of a repository that files are read from. Imported files are
/// read from the root of the importing file.
enum class RootKind : std::uint8_t { Targets, Rules };

/// \brief Location of a file among the repositories: global repository name,
/// root, and normalized path relative to that root. Identifies a file uniquely
/// (e.g., as key of asynchronous maps), independent of how it was referred to.
/// Not to be confused with jstlang::Location, a position in a source file.
struct FileLocation {
    std::string repo;            // global repo name
    std::filesystem::path path;  // relative path from repo root
    RootKind root{RootKind::Targets};

    /// \brief Create location with normalized path (absolute paths are
    /// relative to the root).
    [[nodiscard]] static auto Create(std::string repo,
                                     std::filesystem::path const& path,
                                     RootKind root = RootKind::Targets)
        -> FileLocation {
        return FileLocation{
            .repo = std::move(repo),
            .path = path.lexically_proximate("/").lexically_normal(),
            .root = root};
    }

    /// \brief Location of a file imported from this file, from the same
    /// repository and root. Relative paths are relative to the directory of
    /// this file, absolute paths are relative to the root.
    [[nodiscard]] auto Resolve(std::filesystem::path const& import_path) const
        -> FileLocation {
        return Create(repo,
                      import_path.is_relative()
                          ? path.parent_path() / import_path
                          : import_path,
                      root);
    }

    /// \brief String representation "[<repo>] <path>", or "[<repo> rule_root]
    /// <path>" for the rule root. The repository is always given (an empty
    /// repository name is quoted as ""), the path is separated by a space (so
    /// that "<path>:<line>" in messages can be opened by IDEs).
    [[nodiscard]] auto ToString() const -> std::string {
        auto const* root_suffix = root == RootKind::Rules ? " rule_root" : "";
        return "[" + QuoteRefSegment(repo, RefSegment::Repo) + root_suffix +
               "] " + path.string();
    }

    [[nodiscard]] auto operator==(FileLocation const& other) const noexcept
        -> bool = default;
};

struct FileData {
    using reader_t = std::function<std::optional<FileData>(
        FileLocation const&,           // imported from file
        std::filesystem::path const&,  // file path
        std::string const*)>;          // optional: local repository name

    FileLocation location;
    std::string content;
};

/// \brief Source file, read once and never modified. Owned by an ASTContext
/// (see ast_context.hpp).
struct SourceFile {
    FileLocation location;
    std::string content;
};

}  // namespace jstlang

template <>
struct std::hash<jstlang::FileLocation> {
    [[nodiscard]] auto operator()(
        jstlang::FileLocation const& ref) const noexcept -> std::size_t {
        std::size_t seed{};
        hash_combine<std::string>(&seed, ref.repo);
        hash_combine<std::uint8_t>(&seed, static_cast<std::uint8_t>(ref.root));
        hash_combine<std::filesystem::path>(&seed, ref.path);
        return seed;
    }
};

#endif
