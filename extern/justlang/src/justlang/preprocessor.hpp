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

#ifndef JUSTLANG_PREPROCESSOR_HPP
#define JUSTLANG_PREPROCESSOR_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/ast.hpp"
#include "justlang/file_data.hpp"

namespace justlang {

enum class FileType : std::uint8_t { Plain, Targets, Rules, Expressions };

class Preprocessor {
  public:
    enum class LogType : std::uint8_t { Debug, Error };
    using logger_t = std::function<void(LogType, std::string const&)>;

    explicit Preprocessor(FileData::reader_t reader, logger_t logger)
        : reader_{std::move(reader)}, logger_{std::move(logger)} {}

    [[nodiscard]] auto Process(std::string const& global_repo,
                               std::filesystem::path const& file_path,
                               FileType type) noexcept -> justlang::ASTNodePtr;

    [[nodiscard]] auto Process(FileData const& file_data,
                               FileType type) noexcept -> justlang::ASTNodePtr;

    [[nodiscard]] auto Serialize(justlang::ASTNodePtr const& ast) noexcept
        -> std::optional<nlohmann::json>;

  private:
    FileData::reader_t reader_;
    logger_t logger_;
};

}  // namespace justlang

#endif
