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

#include "lexer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "justlang/ast/native/static_error.hpp"

namespace {

using justlang::TokenType;

std::unordered_map<char, justlang::TokenType> const kSimpleTokens = {
    {'{', TokenType::BRACE_L},
    {'}', TokenType::BRACE_R},
    {'[', TokenType::BRACKET_L},
    {']', TokenType::BRACKET_R},
    {'(', TokenType::PAREN_L},
    {')', TokenType::PAREN_R},
    {'.', TokenType::DOT},
    {',', TokenType::COMMA},
    {';', TokenType::SEMICOLON}};

std::unordered_map<std::string, justlang::TokenType> const kKeywords = {
    {"assert", TokenType::ASSERT},
    {"else", TokenType::ELSE},
    {"error", TokenType::ERROR},
    {"false", TokenType::FALSE},
    {"for", TokenType::FOR},
    {"function", TokenType::FUNCTION},
    {"if", TokenType::IF},
    {"import", TokenType::IMPORT},
    {"in", TokenType::IN},
    {"local", TokenType::LOCAL},
    {"null", TokenType::NULL_LIT},
    {"then", TokenType::THEN},
    {"true", TokenType::TRUE},
};

std::unordered_set<char> const kSymbols =
    {'!', ':', '~', '+', '-', '&', '|', '^', '=', '<', '>', '*', '/', '%'};

std::array<std::string, 6> const kValidBinaryOperators =
    {"==", "!=", "<=", ">=", "&&", "||"};

[[nodiscard]] auto IsSymbol(char opr) -> bool {
    return kSymbols.find(opr) != kSymbols.end();
}

[[nodiscard]] auto TokenTypeToString(justlang::TokenType token_type) {
    switch (token_type) {
        case TokenType::BRACE_L:
            return "\"{\"";
        case TokenType::BRACE_R:
            return "\"}\"";
        case TokenType::BRACKET_L:
            return "\"[\"";
        case TokenType::BRACKET_R:
            return "\"]\"";
        case TokenType::COMMA:
            return "\",\"";
        case TokenType::DOLLAR:
            return "\"$\"";
        case TokenType::DOT:
            return "\".\"";

        case TokenType::PAREN_L:
            return "\"(\"";
        case TokenType::PAREN_R:
            return "\")\"";
        case TokenType::SEMICOLON:
            return "\";\"";

        case TokenType::IDENTIFIER:
            return "IDENTIFIER";
        case TokenType::NUMBER:
            return "NUMBER";
        case TokenType::OPERATOR:
            return "OPERATOR";
        case TokenType::STRING_SINGLE:
            return "STRING_SINGLE";
        case TokenType::STRING_DOUBLE:
            return "STRING_DOUBLE";
        case TokenType::REF_STRING:
            return "REF_STRING";
        case TokenType::TEXT_BLOCK:
            return "TEXT_BLOCK";

        case TokenType::ASSERT:
            return "assert";
        case TokenType::ELSE:
            return "else";
        case TokenType::ERROR:
            return "error";
        case TokenType::FALSE:
            return "false";
        case TokenType::FOR:
            return "for";
        case TokenType::FUNCTION:
            return "function";
        case TokenType::IF:
            return "if";
        case TokenType::IMPORT:
            return "import";
        case TokenType::IMPORTSTR:
            return "importstr";
        case TokenType::IMPORTBIN:
            return "importbin";
        case TokenType::IN:
            return "in";
        case TokenType::LOCAL:
            return "local";
        case TokenType::NULL_LIT:
            return "null";
        case TokenType::THEN:
            return "then";
        case TokenType::TRUE:
            return "true";

        case TokenType::END_OF_FILE:
            return "end of file";
        default:
            // unreachable
            std::terminate();
    }
}

}  // namespace

namespace justlang {

auto Token::operator==(Token const& other) const -> bool {
    return this->type == other.type and this->value == other.value;
}

auto operator<<(std::ostream& output_stream,
                TokenType token_type) -> std::ostream& {
    output_stream << TokenTypeToString(token_type);
    return output_stream;
}

auto operator<<(std::ostream& output_stream,
                Token const& token) -> std::ostream& {
    if (token.value.empty()) {
        output_stream << TokenTypeToString(token.type);
    }
    else if (token.type == TokenType::OPERATOR) {
        output_stream << "\"" << token.value << "\"";
    }
    else {
        output_stream << "(" << TokenTypeToString(token.type) << ", \""
                      << token.value << "\")";
    }
    return output_stream;
}

auto Lexer::Peek() const {
    return source_[current_];
}

auto Lexer::IsAtEnd() const {
    return current_ >= source_.size();
}

auto Lexer::PeekNext() const {
    return current_ + 1 < source_.size() ? source_[current_ + 1] : '\0';
}

void Lexer::Advance() {
    if (Peek() == '\n') {
        line_++;
        column_ = 1;
    }
    else {
        column_++;
    }
    current_++;
}

void Lexer::SkipWhitespace() {
    while (!IsAtEnd() && std::isspace(Peek()) != 0) {
        Advance();
    }
}

auto Lexer::MatchSimpleToken(char token_string) -> std::optional<TokenType> {
    if (auto iter = kSimpleTokens.find(token_string);
        iter != kSimpleTokens.end()) {
        return iter->second;
    }
    return std::nullopt;
}

void Lexer::ReportError(const std::string& message) const {
    throw LexerException(filename_ + ":" + std::to_string(line_) + ":" +
                         std::to_string(column_) + " Error: " + message);
}

// Handle numbers that not included inside "" or ''
auto Lexer::ScanNumber() -> Token {
    auto const start = current_;
    auto is_float = false;

    // Consume integer part
    while (!IsAtEnd() && std::isdigit(Peek()) != 0) {
        Advance();
    }

    // Check for fractional part
    if (!IsAtEnd() && Peek() == '.' && std::isdigit(PeekNext()) != 0) {
        is_float = true;
        Advance();  // Consume the dot

        // Consume fractional part
        while (!IsAtEnd() && std::isdigit(Peek()) != 0) {
            Advance();
        }
    }

    auto number_str = std::string{source_.substr(start, current_ - start)};
    return {filename_,
            is_float ? TokenType::FLOAT : TokenType::INTEGER,
            std::move(number_str),
            LineNumber{line_},
            ColumnNumber{column_}};
}

auto Lexer::ScanLiterals() -> std::optional<Token> {
    // Handle string literals
    char const quote_char = Peek();
    if (quote_char == '"' || quote_char == '\'') {
        Advance();  // Skip opening quote
        std::string value;
        bool escaped = false;

        while (!IsAtEnd()) {
            char const current_char = Peek();

            if (escaped) {
                // Handle escape sequences
                switch (current_char) {
                    case 'n':
                        value += '\n';
                        break;
                    case 't':
                        value += '\t';
                        break;
                    case 'r':
                        value += '\r';
                        break;
                    case '\\':
                        value += '\\';
                        break;
                    case '\'':
                        value += '\'';
                        break;
                    case '"':
                        value += '"';
                        break;
                    default:
                        // Invalid escape sequence
                        value += '\\';  // Keep the backslash
                        value += current_char;
                        break;
                }
                Advance();
                escaped = false;
            }
            else {
                if (current_char == '\\') {
                    escaped = true;
                    Advance();
                }
                else if (current_char == quote_char) {
                    Advance();  // Skip closing quote
                    TokenType const token_type = (quote_char == '"')
                                                     ? TokenType::STRING_DOUBLE
                                                     : TokenType::STRING_SINGLE;
                    return Token{filename_,
                                 token_type,
                                 value,
                                 LineNumber{line_},
                                 ColumnNumber{column_}};
                }
                else {
                    value += current_char;
                    Advance();
                }
            }
        }

        // If we get here, the string wasn't properly terminated
        throw LexerException("Unterminated string literal");
    }

    // If not a string literal, return nullopt
    return std::nullopt;
}

auto Lexer::ScanSpecialString() -> Token {
    Advance();  // Skip @

    // Validate quote
    if (IsAtEnd() || (Peek() != '\'')) {
        ReportError("Expected single-quote after special prefix");
    }

    const char quote = Peek();
    Advance();  // Skip quote

    std::string content;
    bool terminated = false;

    // Extract string content
    while (!IsAtEnd()) {
        if (Peek() == quote) {
            if (PeekNext() == quote) {
                // Handle escaped quote
                content += quote;
                Advance();  // Skip first quote
                Advance();  // Skip second quote
                continue;
            }
            // String termination
            terminated = true;
            Advance();  // Skip closing quote
            break;
        }
        content += Peek();
        Advance();
    }

    if (!terminated) {
        ReportError("Unterminated special string");
    }

    return {filename_,
            TokenType::REF_STRING,
            content,
            LineNumber{line_},
            ColumnNumber{column_}};
}

auto Lexer::ScanKeywords() -> Token {
    std::string keyword;
    bool inKeyword = false;

    while (!IsAtEnd()) {
        char const word = Peek();
        if (static_cast<bool>(std::isalpha(word)) ||
            static_cast<bool>(std::isdigit(word)) || word == '_') {
            keyword += word;
            Advance();
        }
        else {
            break;
        }
    }

    if (kKeywords.find(keyword) != kKeywords.end()) {
        inKeyword = true;
    }

    if (inKeyword && !keyword.empty()) {
        TokenType const type = kKeywords.at(keyword);
        return {
            filename_, type, keyword, LineNumber{line_}, ColumnNumber{column_}};
    }

    return {filename_,
            TokenType::IDENTIFIER,
            keyword,
            LineNumber{line_},
            ColumnNumber{column_}};
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
auto Lexer::HandleTextBlock() -> Token {
    std::string content;
    bool terminated = false;

    // ignore trailing whitespace
    while (!IsAtEnd()) {
        auto const& tok_char = Peek();
        Advance();
        if (tok_char == '\n') {
            break;
        }
        if (std::isspace(tok_char) == 0) {
            ReportError("Unexpected non-whitespace after |||");
        }
    }

    // identify prefix whitespace
    auto prefix_length = std::size_t{};
    while (!IsAtEnd()) {
        auto const& tok_char = Peek();
        if (tok_char == '\n') {
            ReportError(
                "First line of text block (|||) may not contain only "
                "whitespace");
        }
        if (std::isspace(tok_char) == 0) {
            break;
        }
        Advance();
        ++prefix_length;
    }

    if (prefix_length == 0) {
        ReportError(
            "Text block (|||) lines must be prefixed with some non-zero length "
            "whitespace");
    }

    bool read_content{true};
    while (!IsAtEnd()) {
        if (read_content) {
            // read content until next newline
            while (!IsAtEnd()) {
                auto const& tok_char = Peek();
                Advance();
                content += tok_char;
                if (tok_char == '\n') {
                    read_content = false;
                    break;
                }
            }
        }
        else {
            // read whitespace prefix or termination operator
            if (Peek() == '\n') {
                // allow empty lines
                Advance();
                content += '\n';
            }
            else {
                // compute length of whitespace prefix
                auto length = std::size_t{};
                while (!IsAtEnd() and length < prefix_length) {
                    auto const& tok_char = Peek();
                    if (std::isspace(tok_char) == 0 or tok_char == '\n') {
                        break;
                    }
                    Advance();
                    ++length;
                }

                if (length < prefix_length) {
                    // expect termination operator
                    std::string str{};
                    for (int i{}; i < 3 and !IsAtEnd(); ++i) {
                        str += Peek();
                        Advance();
                    }
                    if (str == "|||") {
                        terminated = true;
                        break;
                    }
                    ReportError("Text block is not terminated with |||");
                }

                read_content = true;
            }
        }
    }

    if (!terminated) {
        ReportError("Unterminated text block");
    }

    return {filename_,
            TokenType::TEXT_BLOCK,
            content,
            LineNumber{line_},
            ColumnNumber{column_}};
}

void Lexer::HandleComments() {
    if (PeekNext() == '/') {
        // Single-line comment
        Advance();
        Advance();
        while (!IsAtEnd() && Peek() != '\n') {
            Advance();
        }
        if (Peek() == '\n') {
            Advance();  // skip \n
        }
    }
    else if (PeekNext() == '*') {
        // Multi-line comment
        Advance();
        Advance();
        while (!IsAtEnd() && (Peek() != '*' || PeekNext() != '/')) {
            Advance();
        }
        if (Peek() == '*' && PeekNext() == '/') {
            Advance();
            Advance();
        }
        else {
            ReportError("Unterminated multi-line comment");
        }
    }
}

auto Lexer::HandleOperators() -> Token {
    // Note that we need to handle the following cases:
    // 1. Single operators, such as +, -, =, >, <
    // 2. Dual operators, such as >=, !=, <=, ==
    std::string op_str;

    while (IsSymbol(Peek())) {
        if (Peek() == '=' && (PeekNext() == '-' || PeekNext() == '+')) {
            op_str += Peek();
            Advance();
            break;
        }

        op_str += Peek();
        Advance();
    }

    // Check viability
    if (op_str.size() == 3 and op_str == "|||") {
        // Handle text blocks
        return HandleTextBlock();
    }
    if (op_str.size() == 2 and
        std::find(kValidBinaryOperators.begin(),
                  kValidBinaryOperators.end(),
                  op_str) != kValidBinaryOperators.end()) {
        // Valid dual operators
        return {filename_,
                TokenType::OPERATOR,
                op_str,
                LineNumber{line_},
                ColumnNumber{column_}};
    }
    if (op_str.size() == 1) {
        return {filename_,
                TokenType::OPERATOR,
                op_str,
                LineNumber{line_},
                ColumnNumber{column_}};
    }

    throw LexerException("Error operator:" + op_str);
}

// Token scanning implementation
auto Lexer::ScanToken() -> std::optional<Token> {
    const char token_char = Peek();

    // Handle simple single-character tokens
    if (auto type = MatchSimpleToken(token_char)) {
        std::string content;
        content += token_char;
        Advance();
        return Token{filename_,
                     *type,
                     content,
                     LineNumber{line_},
                     ColumnNumber{column_}};
    }

    // Handle literals (strings)
    if (token_char == '"' || token_char == '\'') {
        return ScanLiterals();
    }

    // Handle operators
    if (IsSymbol(token_char)) {
        return HandleOperators();
    }

    // Handle numbers
    if (std::isdigit(token_char) != 0) {
        return ScanNumber();
    }

    // Handle special strings
    if (token_char == '@') {
        return ScanSpecialString();
    }

    // Handle keywords
    if (static_cast<bool>(std::isalpha(token_char)) || token_char == '_') {
        return ScanKeywords();
    }

    // Unknown character
    Advance();
    ReportError(std::string{"Unexpected character: '"} + token_char + "'");
}

auto Lexer::Tokenize() -> Tokens {
    Tokens tokens;

    while (!IsAtEnd()) {
        // Handle comments: // and /*
        SkipWhitespace();
        if (Peek() == '/' && (PeekNext() == '/' || PeekNext() == '*')) {
            HandleComments();
            continue;
        }
        if (IsAtEnd()) {
            break;
        }

        if (auto token = ScanToken()) {
            token->line = LineNumber{line_};
            token->column = ColumnNumber{column_};
            tokens.emplace_back(std::move(*token));
        }
    }

    return tokens;
}

auto Lexer::UnTokenize(const Tokens& tokens) -> std::string {
    std::ostringstream output;
    for (const auto& tok : tokens) {
        if (tok.type == TokenType::STRING_DOUBLE) {
            output << "\"" << tok.value << "\"\n";
        }
        else if (tok.type == TokenType::STRING_SINGLE) {
            output << "'" << tok.value << "'\n";
        }
        else if (tok.type == TokenType::TEXT_BLOCK) {
            output << "|||\n" << tok.value << "|||\n";
        }
        else {
            output << tok.value << "\n";
        }
    }
    return output.str();
}

}  // namespace justlang
