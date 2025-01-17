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

#ifndef JUSTLANG_AST_INLINE_HPP
#define JUSTLANG_AST_INLINE_HPP

#include <stdexcept>
#include <string>

#include "justlang/ast/ast.hpp"

namespace justlang {

class ASTInlineError : public std::runtime_error {
  public:
    explicit ASTInlineError(std::string const& error);
    explicit ASTInlineError(std::string const& error,
                            justlang::Location const& loc);
};

// Inline all functions and variables.
[[nodiscard]] auto InlineAST(justlang::ASTNodePtr const& expr)
    -> justlang::ASTNodePtr;

}  // namespace justlang

#endif
