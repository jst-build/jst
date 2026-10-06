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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_PREPROCESSOR_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_PREPROCESSOR_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "gsl/gsl"
#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/preproc/expr_serializer.hpp"

namespace jstlang {

class Preprocessor {
  public:
    enum class LogType : std::uint8_t { Debug, Error };
    using logger_t = std::function<void(LogType, std::string const&)>;

    explicit Preprocessor(FileData::reader_t reader, logger_t logger)
        : reader_{std::move(reader)}, logger_{std::move(logger)} {}

    /// \brief Parse and inline file, which is read via the reader.
    /// \param context The context of the returned AST, which must outlive the
    ///                AST and its uses, including serialization (see
    ///                ASTContext).
    /// \returns nullptr on error (reported via logger).
    [[nodiscard]] auto Process(gsl::not_null<ASTContext*> const& context,
                               std::string const& global_repo,
                               std::filesystem::path const& file_path,
                               FileType type) noexcept -> jstlang::ASTNodePtr;

    /// \brief Parse and inline given file data, see above.
    [[nodiscard]] auto Process(gsl::not_null<ASTContext*> const& context,
                               FileData const& file_data,
                               FileType type) noexcept -> jstlang::ASTNodePtr;

    /// \brief Serialize the (processed) AST of a file of the given type to
    /// code of the expression language (see ExprSerializer).
    [[nodiscard]] auto Serialize(jstlang::ASTNodePtr const& ast,
                                 FileType type) noexcept
        -> std::optional<nlohmann::json>;

  private:
    FileData::reader_t reader_;
    logger_t logger_;
};

}  // namespace jstlang

#endif
