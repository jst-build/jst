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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_PARSER_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_PARSER_HPP

#include <memory>
#include <stdexcept>

#include "gsl/gsl"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/ast_context.hpp"
#include "src/buildtool/jstlang/file_data.hpp"

namespace jstlang {

class Parser;
class ParserImpl;

using ParserPtr = std::unique_ptr<Parser>;

class ASTParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Parser {
  public:
    /// \brief Create parser.
    /// Imports are parsed recursively (via reader), unless they are deferred:
    /// then imports are unresolved references (ForeignNode).
    /// \param context       The context of the created AST nodes, which must
    ///                      outlive the nodes and their uses (see ASTContext).
    /// \param reader        Reads the files to parse (incl. imports).
    /// \param defer_imports Defer resolving import nodes (keeps ForeignNode).
    Parser(gsl::not_null<ASTContext*> const& context,
           FileData::reader_t reader,
           bool defer_imports = false);
    Parser(Parser const&) = delete;
    Parser(Parser&&) = delete;
    ~Parser();

    auto operator=(Parser const&) = delete;
    auto operator=(Parser&&) = delete;

    [[nodiscard]] auto ParseData(FileData const& file_data) -> ASTNodePtr;

  private:
    std::unique_ptr<ParserImpl> impl_;
};

/// \brief Files of the unresolved imports of an AST (parsed with deferred
/// imports), in order of appearance.
[[nodiscard]] auto CollectImports(ASTNodePtr const& ast)
    -> std::vector<FileLocation>;

}  // namespace jstlang

#endif
