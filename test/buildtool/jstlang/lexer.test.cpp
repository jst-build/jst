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

#include "src/buildtool/jstlang/ast/native/lexer.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
namespace {

void testBasic(const char* name,
               const char* input,
               const jstlang::Tokens& tokens,
               const std::string& error) {
    jstlang::Tokens expected_tokens(tokens);
    // expected_tokens.emplace_back(jstlang::TokenType::END_OF_FILE, "");
    jstlang::Lexer lexer(name, std::string_view(input));

    try {
        jstlang::Tokens lexed_tokens = lexer.Tokenize();
        REQUIRE(expected_tokens == lexed_tokens);
    } catch (const std::exception& e) {
        std::cout << "error" << error << '\n';
    }
}

void testComplex(const char* name, const char* input) {
    jstlang::Lexer lexer(name, std::string_view(input));
    try {
        jstlang::Tokens const lexed_tokens = lexer.Tokenize();
        std::cout << jstlang::Lexer::UnTokenize(lexed_tokens) << '\n';
    } catch (const std::exception& e) {
        std::cout << "error during tokenizing" << '\n';
    }
}

TEST_CASE("TestOperators", "[basic_operators]") {
    SECTION("TestOperators") {
        testBasic("brace L",
                  "{",
                  {jstlang::Token(jstlang::TokenType::BRACE_L, "{")},
                  "");
        testBasic("brace R",
                  "}",
                  {jstlang::Token(jstlang::TokenType::BRACE_R, "}")},
                  "");
        testBasic("bracket L",
                  "[",
                  {jstlang::Token(jstlang::TokenType::BRACKET_L, "[")},
                  "");
        testBasic("bracket R",
                  "]",
                  {jstlang::Token(jstlang::TokenType::BRACKET_R, "]")},
                  "");
        testBasic("colon ",
                  ":",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, ":")},
                  "");
        testBasic(
            "comma", ",", {jstlang::Token(jstlang::TokenType::COMMA, ",")}, "");
        testBasic(
            "dot", ".", {jstlang::Token(jstlang::TokenType::DOT, ".")}, "");
        testBasic("paren L",
                  "(",
                  {jstlang::Token(jstlang::TokenType::PAREN_L, "(")},
                  "");
        testBasic("paren R",
                  ")",
                  {jstlang::Token(jstlang::TokenType::PAREN_R, ")")},
                  "");
        testBasic("semicolon",
                  ";",
                  {jstlang::Token(jstlang::TokenType::SEMICOLON, ";")},
                  "");
        testBasic("not 1",
                  "!",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, "!")},
                  "");
        testBasic("not 2",
                  "! ",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, "!")},
                  "");
        testBasic("not equal",
                  "!=",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, "!=")},
                  "");
        testBasic("tilde",
                  "~",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, "~")},
                  "");
        testBasic("plus",
                  "+",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, "+")},
                  "");
        testBasic("minus",
                  "-",
                  {jstlang::Token(jstlang::TokenType::OPERATOR, "-")},
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
