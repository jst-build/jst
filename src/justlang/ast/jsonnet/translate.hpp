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

#ifndef JUSTLANG_JSONNET_AST_TRANSLATE_HPP
#define JUSTLANG_JSONNET_AST_TRANSLATE_HPP

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include <ast.h>           // jsonnet
#include <static_error.h>  // jsonnet

#include "justlang/ast/ast.hpp"
#include "justlang/file_data.hpp"

namespace justlang::jsonnet {

class ASTTranslateError : public std::runtime_error {
  public:
    explicit ASTTranslateError(std::string const& error);
    explicit ASTTranslateError(std::string const& error,
                               ::jsonnet::internal::LocationRange const& loc);
};

struct AstData {
    FileLocation origin;
    ::jsonnet::internal::AST* ast{};
};

using import_callback_t = std::function<std::optional<AstData>(
    FileLocation const&,   // imported from file
    std::string const&,    // file path
    std::string const*)>;  // optional: local repository name

[[nodiscard]] auto TranslateToJust(::jsonnet::internal::AST const* expr,
                                   justlang::FileLocation input_file,
                                   import_callback_t import_callback)
    -> ASTNodePtr;

}  // namespace justlang::jsonnet

#endif
