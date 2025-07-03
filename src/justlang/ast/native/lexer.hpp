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

#ifndef JUSTLANG_AST_NATIVE_LEXER_HPP
#define JUSTLANG_AST_NATIVE_LEXER_HPP

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace justlang {

enum class TokenType : std::uint8_t {
    // Symbols
    BRACE_L,
    BRACE_R,
    BRACKET_L,
    BRACKET_R,
    COMMA,
    DOLLAR,
    DOT,
    PAREN_L,
    PAREN_R,
    SEMICOLON,

    // Arbitrary length lexemes
    IDENTIFIER,
    NUMBER,
    OPERATOR,
    STRING_DOUBLE,
    STRING_SINGLE,
    TEXT_BLOCK,
    REF_STRING,
    VAR_STRING,

    // Numbers
    FLOAT,
    INTEGER,

    // Keywords
    ASSERT,
    ELSE,
    ERROR,
    FALSE,
    FOR,
    FUNCTION,
    IF,
    IMPORT,
    IMPORTSTR,
    IMPORTBIN,
    IN,
    LOCAL,
    NULL_LIT,
    THEN,
    TRUE,
    AT_SIGN,

    // A special token that holds line/column information about the end of the
    // file.
    END_OF_FILE
};

auto operator<<(std::ostream& output_stream,
                TokenType token_type) -> std::ostream&;

struct LineNumber {
    LineNumber() = default;
    explicit LineNumber(std::size_t value) : value_(value) {}
    constexpr operator std::size_t() const { return value_; }  // NOLINT
  private:
    std::size_t value_{};
};

struct ColumnNumber {
    ColumnNumber() = default;
    explicit ColumnNumber(std::size_t value) : value_(value) {}
    constexpr operator std::size_t() const { return value_; }  // NOLINT
  private:
    std::size_t value_{};
};

struct Token {
    std::string file;
    TokenType type;
    std::string value;
    LineNumber line;
    ColumnNumber column;

    Token(std::string filename,
          TokenType token_type,
          std::string token_value,
          LineNumber line = {},
          ColumnNumber column = {})
        : file(std::move(filename)),
          type(token_type),
          value(std::move(token_value)),
          line(line),
          column(column) {}

    Token(TokenType token_type,
          std::string token_value,
          LineNumber line = {},
          ColumnNumber column = {})
        : type(token_type),
          value(std::move(token_value)),
          line(line),
          column(column) {}

    [[nodiscard]] auto operator==(Token const& other) const -> bool;
    friend auto operator<<(std::ostream& output_stream,
                           Token const& token) -> std::ostream&;
};

using Tokens = std::vector<Token>;

class Lexer {
  public:
    explicit Lexer(std::string filename = "", std::string_view source = "")
        : source_{source}, filename_{std::move(filename)} {}

    Lexer(Lexer const&) = delete;
    Lexer(Lexer&&) = default;
    ~Lexer() = default;

    auto operator=(Lexer const&) = delete;
    auto operator=(Lexer&&) -> Lexer& = default;

    [[nodiscard]] auto Tokenize() -> Tokens;
    [[nodiscard]] static auto UnTokenize(Tokens const& tokens) -> std::string;

  private:
    std::string_view source_;
    std::string filename_;
    std::size_t current_{};
    std::size_t line_{1};
    std::size_t column_{1};

    // Helper methods
    [[nodiscard]] auto IsAtEnd() const;
    [[nodiscard]] auto Peek() const;
    [[nodiscard]] auto PeekNext() const;
    void Advance();
    void SkipWhitespace();

    // Token scanning
    [[nodiscard]] static auto MatchSimpleToken(char token_string)
        -> std::optional<TokenType>;
    [[nodiscard]] auto ScanToken() -> std::optional<Token>;
    [[nodiscard]] auto ScanLiterals() -> std::optional<Token>;
    [[nodiscard]] auto ScanNumber() -> Token;
    [[nodiscard]] auto ScanSpecialString() -> Token;
    [[nodiscard]] auto ScanKeywords() -> Token;
    [[nodiscard]] auto HandleTextBlock() -> Token;
    [[nodiscard]] auto HandleOperators() -> Token;
    void HandleComments();

    [[noreturn]] void ReportError(std::string const& message) const;
};

}  // namespace justlang

#endif  // JUSTLANG_AST_NATIVE_LEXER_HPP
