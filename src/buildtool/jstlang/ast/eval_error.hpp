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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_ERROR_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_ERROR_HPP

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "src/buildtool/jstlang/ast/ast.hpp"

namespace jstlang {

/// \brief Error of an evaluation, with the location of its cause. It may
/// carry the values involved in the error (e.g., the conflicting values of a
/// disjoint union), so that the caller can describe them further (e.g., where
/// those artifacts come from).
class ASTEvalError : public std::runtime_error {
  public:
    explicit ASTEvalError(std::string const& error,
                          std::vector<ASTNodePtr> involved_values = {})
        : runtime_error{error}, involved_values_{std::move(involved_values)} {}

    /// \brief Error at a location, optionally caused by another error (e.g.,
    /// of a called function), which is reported after the location of this
    /// error, so that nested errors read from the outermost to the innermost
    /// location.
    explicit ASTEvalError(std::string const& error,
                          jstlang::Location const& loc,
                          std::string const& cause = "",
                          std::vector<ASTNodePtr> involved_values = {})
        : runtime_error{GetLocationString(loc) + ": " + error +
                        GetLocationSnippet(loc) +
                        (cause.empty() ? "" : "\n" + cause)},
          involved_values_{std::move(involved_values)} {}

    [[nodiscard]] auto InvolvedValues() const noexcept
        -> std::vector<ASTNodePtr> const& {
        return involved_values_;
    }

  private:
    std::vector<ASTNodePtr> involved_values_;

    [[nodiscard]] static auto GetLocationString(jstlang::Location const& loc)
        -> std::string;
    [[nodiscard]] static auto GetLocationSnippet(jstlang::Location const& loc)
        -> std::string;
};

inline auto ASTEvalError::GetLocationString(jstlang::Location const& loc)
    -> std::string {
    std::ostringstream oss{};
    oss << loc.File();
    oss << ":" << loc.line;
    oss << ":" << loc.column;
    if (loc.column_end > 0) {
        oss << "-" << loc.column_end;
    }
    return oss.str();
}

inline auto ASTEvalError::GetLocationSnippet(jstlang::Location const& loc)
    -> std::string {
    std::ostringstream oss{};
    if (loc.source != nullptr) {
        auto const& content = loc.source->content;
        auto line_pos = 0UL;
        auto line_end = content.find('\n', line_pos);
        for (std::size_t i{1}; i < loc.line and line_end != std::string::npos;
             ++i) {
            line_pos = line_end + 1;
            line_end = content.find('\n', line_pos);
        }
        if (line_end != std::string::npos) {
            static auto const kPrefix = " |" + std::string(2, ' ');
            auto line_str = std::to_string(loc.line);
            auto line_data = content.substr(line_pos, line_end - line_pos);
            auto const column_end =
                loc.column_end > 0 ? loc.column_end : line_data.length();
            oss << "\n\n";
            oss << line_str + kPrefix;
            oss << line_data;
            oss << "\n";
            oss << std::string(line_str.length(), ' ') + kPrefix;
            oss << std::string(loc.column - 1, ' ');
            oss << std::string(column_end - loc.column + 1, '^');
            oss << "\n";
        }
    }
    return oss.str();
}

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_ERROR_HPP