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

#ifndef JUSTLANG_AST_NATIVE_PARSER_HPP
#define JUSTLANG_AST_NATIVE_PARSER_HPP

#include <concepts>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "justlang/ast/ast.hpp"
#include "justlang/ast/native/lexer.hpp"
#include "justlang/ast/parser.hpp"
#include "justlang/file_data.hpp"

namespace justlang {

class NativeParser final : public Parser {
  public:
    NativeParser(FileData::reader_t reader,
                 std::vector<FileLocation> import_chain)
        : reader_{std::move(reader)}, import_chain_{std::move(import_chain)} {}
    NativeParser(const NativeParser&) = delete;
    NativeParser(NativeParser&&) = delete;
    ~NativeParser() final = default;

    auto operator=(const NativeParser&) = delete;
    auto operator=(NativeParser&&) = delete;

    [[nodiscard]] auto ParseData(const FileData& file_data)
        -> justlang::ASTNodePtr final;

  private:
    class TokenStream {
      public:
        TokenStream() = default;
        explicit TokenStream(Tokens tokens) : tokens_{std::move(tokens)} {}

        void Advance();
        [[nodiscard]] auto Size() const -> std::size_t;
        [[nodiscard]] auto Peek() const -> Token const&;
        [[nodiscard]] auto PeekNext() const -> Token const&;
        [[nodiscard]] auto Pop() -> Token const&;
        auto PopCheck(TokenType token_type,
                      char const* data = nullptr) -> Token const&;

      private:
        Tokens tokens_{};
        std::size_t pos_{};
    };

    std::string filename_;
    FileData::reader_t reader_;
    std::vector<justlang::FileLocation> import_chain_;
    TokenStream tokens_{};

    [[nodiscard]] auto Parse(unsigned priority) -> ASTNodePtr;
    [[nodiscard]] auto ParseSimple() -> ASTNodePtr;
    [[nodiscard]] auto ParseInfix(ASTNodePtr const& prev_node,
                                  Token const& curr_token,
                                  unsigned priority) -> ASTNodePtr;
    [[nodiscard]] auto ParseContent(Token const& begin) -> ASTNodePtr;
    [[nodiscard]] auto ParseLocalImport() -> ASTNodePtr;
    template <class TParams>
        requires(std::same_as<TParams, FuncNode::params_t> ||
                 std::same_as<TParams, CallNode::params_t>)
    [[nodiscard]] auto ParseArgs() -> TParams;
    [[nodiscard]] auto ParseFuncArgs() -> FuncNode::params_t;
    [[nodiscard]] auto ParseCallArgs() -> CallNode::params_t;
};

}  // namespace justlang

#endif  // JUSTLANG_AST_NATIVE_PARSER_HPP
