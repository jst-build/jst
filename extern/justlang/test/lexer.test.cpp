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

#include "justlang/ast/native/lexer.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
namespace {

void testBasic(const char* name,
               const char* input,
               const justlang::Tokens& tokens,
               const std::string& error) {
    justlang::Tokens expected_tokens(tokens);
    // expected_tokens.emplace_back(justlang::TokenType::END_OF_FILE, "");
    justlang::Lexer lexer(name, std::string_view(input));

    try {
        justlang::Tokens lexed_tokens = lexer.Tokenize();
        REQUIRE(expected_tokens == lexed_tokens);
    } catch (const std::exception& e) {
        std::cout << "error" << error << '\n';
    }
}

void testComplex(const char* name, const char* input) {
    justlang::Lexer lexer(name, std::string_view(input));
    try {
        justlang::Tokens const lexed_tokens = lexer.Tokenize();
        std::cout << justlang::Lexer::UnTokenize(lexed_tokens) << '\n';
    } catch (const std::exception& e) {
        std::cout << "error during tokenizing" << '\n';
    }
}

TEST_CASE("TestOperators", "[basic_operators]") {
    SECTION("TestOperators") {
        testBasic("brace L",
                  "{",
                  {justlang::Token(justlang::TokenType::BRACE_L, "{")},
                  "");
        testBasic("brace R",
                  "}",
                  {justlang::Token(justlang::TokenType::BRACE_R, "}")},
                  "");
        testBasic("bracket L",
                  "[",
                  {justlang::Token(justlang::TokenType::BRACKET_L, "[")},
                  "");
        testBasic("bracket R",
                  "]",
                  {justlang::Token(justlang::TokenType::BRACKET_R, "]")},
                  "");
        testBasic("colon ",
                  ":",
                  {justlang::Token(justlang::TokenType::OPERATOR, ":")},
                  "");
        testBasic("comma",
                  ",",
                  {justlang::Token(justlang::TokenType::COMMA, ",")},
                  "");
        testBasic(
            "dot", ".", {justlang::Token(justlang::TokenType::DOT, ".")}, "");
        testBasic("paren L",
                  "(",
                  {justlang::Token(justlang::TokenType::PAREN_L, "(")},
                  "");
        testBasic("paren R",
                  ")",
                  {justlang::Token(justlang::TokenType::PAREN_R, ")")},
                  "");
        testBasic("semicolon",
                  ";",
                  {justlang::Token(justlang::TokenType::SEMICOLON, ";")},
                  "");
        testBasic("not 1",
                  "!",
                  {justlang::Token(justlang::TokenType::OPERATOR, "!")},
                  "");
        testBasic("not 2",
                  "! ",
                  {justlang::Token(justlang::TokenType::OPERATOR, "!")},
                  "");
        testBasic("not equal",
                  "!=",
                  {justlang::Token(justlang::TokenType::OPERATOR, "!=")},
                  "");
        testBasic("tilde",
                  "~",
                  {justlang::Token(justlang::TokenType::OPERATOR, "~")},
                  "");
        testBasic("plus",
                  "+",
                  {justlang::Token(justlang::TokenType::OPERATOR, "+")},
                  "");
        testBasic("minus",
                  "-",
                  {justlang::Token(justlang::TokenType::OPERATOR, "-")},
                  "");
    }
}
TEST_CASE("TestComplex", "[complex_targets]") {
    SECTION("TestComplex") {
        const char* content =
            "local foo=5;\n"
            "{\n"
            "  bar: foo,\n"
            "}\n"; /*initial data*/

        testComplex("local", content);
    }
}

}  // namespace
