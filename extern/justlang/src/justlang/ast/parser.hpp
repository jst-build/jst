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

#ifndef JUSTLANG_AST_PARSER_HPP
#define JUSTLANG_AST_PARSER_HPP

#include <memory>
#include <stdexcept>

#include "justlang/ast/ast.hpp"
#include "justlang/file_data.hpp"

namespace justlang {

class Parser;

using ParserPtr = std::unique_ptr<Parser>;

class ASTParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Parser {
  public:
    [[nodiscard]] static auto Create(FileData::reader_t reader) -> ParserPtr;

    Parser() = default;
    Parser(Parser const&) = delete;
    Parser(Parser&&) = delete;
    virtual ~Parser() = default;

    auto operator=(Parser const&) = delete;
    auto operator=(Parser&&) = delete;

    [[nodiscard]] virtual auto ParseData(FileData const& file_data)
        -> ASTNodePtr = 0;
};

}  // namespace justlang

#endif
