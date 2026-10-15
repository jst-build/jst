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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_EXPR_SERIALIZER_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_EXPR_SERIALIZER_HPP

#include <cstdint>
#include <stdexcept>

#include "nlohmann/json.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"

namespace jstlang {

/// \brief Type of a jstlang file, which determines the structure of its
/// serialization.
enum class FileType : std::uint8_t { Plain, Targets, Rules, Expressions };

class ExprSerializerError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

/// \brief Serializer of (inlined) ASTs to code of the expression language.
/// For files of definitions, the structure of the AST is checked and the parts
/// that are not evaluated (e.g., the names of targets or their field "type")
/// are serialized verbatim, i.e., as plain JSON instead of as expressions.
class ExprSerializer final {
  public:
    explicit ExprSerializer(FileType type) noexcept : type_{type} {}

    /// \brief Serialize the AST of a file of the serializer's type.
    /// \throws ExprSerializerError if the AST does not have the structure
    /// required by the file type, std::exception on other errors.
    [[nodiscard]] auto Serialize(ASTNode const& ast) const -> nlohmann::json;

  private:
    FileType type_;
};

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_PREPROC_EXPR_SERIALIZER_HPP
