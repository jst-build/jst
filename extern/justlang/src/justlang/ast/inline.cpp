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

#include "justlang/ast/inline.hpp"

#include <cstddef>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "justlang/ast/ast.hpp"
#include "justlang/ast/inline_visitor.hpp"

namespace {

using justlang::ASTInlineError;

[[nodiscard]] auto GetLocationString(justlang::Location const& loc)
    -> std::string {
    std::ostringstream oss{};
    oss << loc.file;
    oss << ":" << loc.line;
    oss << ":" << loc.column;
    if (loc.column_end > 0) {
        oss << "-" << loc.column_end;
    }
    return oss.str();
}

[[nodiscard]] auto GetLocationSnippet(justlang::Location const& loc)
    -> std::string {
    std::ostringstream oss{};
    if (loc.content) {
        auto line_pos = 0UL;
        auto line_end = loc.content->find('\n', line_pos);
        for (std::size_t i{1}; i < loc.line and line_end != std::string::npos;
             ++i) {
            line_pos = line_end + 1;
            line_end = loc.content->find('\n', line_pos);
        }
        if (line_end != std::string::npos) {
            static auto const kPrefix = " |" + std::string(2, ' ');
            auto line_str = std::to_string(loc.line);
            auto line_data = loc.content->substr(line_pos, line_end - line_pos);
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

}  // namespace

ASTInlineError::ASTInlineError(std::string const& error)
    : runtime_error{error} {}

ASTInlineError::ASTInlineError(std::string const& error,
                               justlang::Location const& loc)
    : runtime_error{GetLocationString(loc) + ": " + error +
                    GetLocationSnippet(loc)} {}

auto justlang::InlineAST(justlang::ASTNodePtr const& expr)
    -> justlang::ASTNodePtr {
    auto scope = ScopeDefs{};
    return ASTInlineVisitor{&scope, /*partial_inline=*/false}.InlineFunctions(
        expr);
}
