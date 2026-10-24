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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_PREPROCESSOR_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_PREPROCESSOR_HPP

#include <cstdint>
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

class Parser;

class Preprocessor {
  public:
    enum class LogType : std::uint8_t { Debug, Error };
    using logger_t = std::function<void(LogType, std::string const&)>;

    /// \param reader  Reads the files to process (incl. imports).
    /// \param logger  Reports debug output and errors.
    /// \param repos   Resolves the repository names of imports from other
    ///                repositories (see Parser); if empty, such imports are
    ///                not supported.
    explicit Preprocessor(FileReader reader,
                          logger_t logger,
                          RepoResolver repos = nullptr)
        : reader_{std::move(reader)},
          logger_{std::move(logger)},
          repos_{std::move(repos)} {}

    /// \brief Parse and inline file, which is read via the reader.
    /// \param context The context of the returned AST, which must outlive the
    ///                AST and its uses, including serialization (see
    ///                ASTContext).
    /// \returns nullptr on error (reported via logger).
    [[nodiscard]] auto Process(gsl::not_null<ASTContext*> const& context,
                               FileLocation const& location,
                               FileType type) noexcept -> jstlang::ASTNodePtr;

    /// \brief Parse and inline given content of file, see above.
    [[nodiscard]] auto Process(gsl::not_null<ASTContext*> const& context,
                               FileLocation location,
                               std::string content,
                               FileType type) noexcept -> jstlang::ASTNodePtr;

    /// \brief Serialize the (processed) AST of a file of the given type to
    /// code of the expression language (see ExprSerializer).
    [[nodiscard]] auto Serialize(jstlang::ASTNodePtr const& ast,
                                 FileType type) noexcept
        -> std::optional<nlohmann::json>;

  private:
    FileReader reader_;
    logger_t logger_;
    RepoResolver repos_;

    [[nodiscard]] auto ProcessWith(
        gsl::not_null<ASTContext*> const& context,
        std::function<ASTNodePtr(Parser&)> const& parse,
        FileType type) noexcept -> jstlang::ASTNodePtr;
};

}  // namespace jstlang

#endif
