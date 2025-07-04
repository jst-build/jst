// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef JUSTLANG_FILE_DATA_HPP
#define JUSTLANG_FILE_DATA_HPP

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "justlang/ref.hpp"

namespace justlang {

struct FileLocation {
    std::string repo;            // global repo name
    std::filesystem::path path;  // relative path from repo root
    std::shared_ptr<std::string> content{};

    [[nodiscard]] auto ToString() const noexcept -> std::string {
        auto repo_prefix = std::string{};
        if (not repo.empty()) {
            repo_prefix = "[" + QuoteRefSegment(repo, RefSegment::Repo) + "] ";
        }
        return repo_prefix + path.string();
    }

    [[nodiscard]] auto operator==(FileLocation const& other) const noexcept
        -> bool {
        return repo == other.repo and path == other.path;
    }
};

struct FileData {
    using reader_t = std::function<std::optional<FileData>(
        FileLocation const&,           // imported from file
        std::filesystem::path const&,  // file path
        std::string const*)>;          // optional: local repository name

    FileLocation location;
    std::string content;
};

}  // namespace justlang

#endif
