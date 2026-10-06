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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_AST_CONTEXT_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_AST_CONTEXT_HPP

#include <deque>
#include <string>
#include <utility>

#include "src/buildtool/jstlang/file_data.hpp"

namespace jstlang {

/// \brief Data shared by the AST nodes created with this context (by the
/// parser), such as the parsed source files.
///
/// IMPORTANT: The context must outlive all AST nodes created with it and all
/// uses of those nodes, including their evaluation. Nodes do not keep the
/// context alive.
///
/// The context is not thread-safe; use one context per parse. Nodes created
/// with different contexts may be combined (e.g., when evaluating imports),
/// as long as all of those contexts are alive.
class ASTContext final {
  public:
    ASTContext() = default;
    // AST nodes refer to the context's data
    ASTContext(ASTContext const&) = delete;
    ASTContext(ASTContext&&) = delete;
    ~ASTContext() = default;
    auto operator=(ASTContext const&) -> ASTContext& = delete;
    auto operator=(ASTContext&&) -> ASTContext& = delete;

    /// \brief Add a source file, which is owned by the context.
    /// \returns Reference to the source file, valid as long as the context.
    [[nodiscard]] auto AddSource(FileLocation location,
                                 std::string content) -> SourceFile const& {
        return sources_.emplace_back(SourceFile{.location = std::move(location),
                                                .content = std::move(content)});
    }

  private:
    std::deque<SourceFile> sources_;  // stable addresses
};

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_AST_CONTEXT_HPP
