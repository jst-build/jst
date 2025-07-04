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

#include "justlang/ast/native/parser.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "justlang/ast/ast.hpp"
#include "justlang/ast/native/lexer.hpp"
#include "justlang/ast/native/static_error.hpp"
#include "justlang/ast/parser.hpp"
#include "justlang/file_data.hpp"
#include "justlang/ref.hpp"

namespace {

using BinaryOp = justlang::BinaryOperationNode::Type;
using UnaryOp = justlang::UnaryOperationNode::Type;

constexpr int kApplyPriority = 2;  // Function calls and indexing.
constexpr int kUnaryPriority = 4;  // Logical and bitwise negation, unary + -
constexpr int kMaxPriority = 15;   // higher than any other precedence

std::unordered_map<std::string, BinaryOp> const kBinaryMap = {
    {"-",
     BinaryOp::Plus},  // we treat binaryop minus as plus wrapped by unary minus
    {"+", BinaryOp::Plus},
    {"==", BinaryOp::Equal},
    {"!=", BinaryOp::Equal},
    {"&&", BinaryOp::And},
    {"||", BinaryOp::Or},
    {"*", BinaryOp::Multiplication}};

std::unordered_map<std::string, UnaryOp> const kUnaryMap = {
    {"!", UnaryOp::Not},
    {"-", UnaryOp::Minus},
};

std::unordered_map<BinaryOp, unsigned> const kPrecedenceMap = {
    {BinaryOp::Plus, 6},
    {BinaryOp::Equal, 9},
    {BinaryOp::And, 10},
    {BinaryOp::Or, 14},
    {BinaryOp::Multiplication, 5}};

[[nodiscard]] auto OpIsBinary(std::string const& opr)
    -> std::optional<BinaryOp> {
    auto itr = kBinaryMap.find(opr);
    if (itr == kBinaryMap.end()) {
        return std::nullopt;
    }
    return itr->second;
}

[[nodiscard]] auto OpIsUnary(std::string const& opr) -> std::optional<UnaryOp> {
    auto itr = kUnaryMap.find(opr);
    if (itr == kUnaryMap.end()) {
        return std::nullopt;
    }
    return itr->second;
}

[[nodiscard]] auto CreateLocation(const justlang::Token& begin,
                                  const justlang::FileLocation& origin)
    -> justlang::Location {
    return {.file = begin.file,
            .line = begin.line,
            .column = begin.column,
            .column_end = begin.column,
            .content = origin.content};
}

[[nodiscard]] auto CreateLocation(const justlang::Token& begin,
                                  const justlang::Token& end,
                                  const justlang::FileLocation& origin)
    -> justlang::Location {
    return {.file = begin.file,
            .line = begin.line,
            .column = begin.column,
            .column_end = begin.column <= end.column ? end.column : 0UL,
            .content = origin.content};
}

[[nodiscard]] auto CreateLocation(const justlang::Token& begin,
                                  const justlang::ASTNodePtr& end,
                                  const justlang::FileLocation& origin)
    -> justlang::Location {

    if (end == nullptr) {
        throw justlang::ParserException(
            "Empty AST node while creating location");
    }

    return {.file = begin.file,
            .line = begin.line,
            .column = begin.column,
            .column_end = begin.column <= end->GetLocation().column_end
                              ? end->GetLocation().column_end
                              : 0,
            .content = origin.content};
}

[[nodiscard]] auto MakeOperationNode(justlang::Location const& loc,
                                     justlang::ASTNodePtr const& lhs,
                                     justlang::ASTNodePtr const& rhs,
                                     BinaryOp bop,
                                     bool negate,
                                     bool minus) -> justlang::ASTNodePtr {
    justlang::ValueType eval_type = justlang::ValueType::Any;
    auto rhs_node = rhs;
    switch (bop) {
        case BinaryOp::Equal:
        case BinaryOp::And:
        case BinaryOp::Or:
            eval_type = justlang::ValueType::Bool;
            break;
        case BinaryOp::Multiplication:
            eval_type = justlang::ValueType::Number;
            break;
        case BinaryOp::Plus:
            if (minus) {
                rhs_node = std::make_shared<justlang::UnaryOperationNode>(
                    loc, justlang::UnaryOperationNode::Type::Minus, rhs);
                eval_type = justlang::ValueType::Number;
            }
            else {
                eval_type = justlang::ValueType::Any;
            }
            break;
        default:
            throw justlang::ParserException("Unsupported binary operator", loc);
    }

    justlang::ASTNodePtr node = std::make_shared<justlang::BinaryOperationNode>(
        loc, bop, lhs, std::move(rhs_node), eval_type);

    if (negate) {
        node = std::make_shared<justlang::UnaryOperationNode>(
            loc, justlang::UnaryOperationNode::Type::Not, std::move(node));
    }

    return node;
}

[[nodiscard]] auto Unexpected(const justlang::Token& tok,
                              const std::string& msg)
    -> justlang::ParserException {
    std::ostringstream strs;
    strs << tok.file << "(" << tok.line << ":" << tok.column << ")"
         << " unexpected " << "(" << tok.type << ":" << tok.value << ")"
         << " while " << msg;
    return justlang::ParserException{strs.str()};
}

}  // namespace

namespace justlang {

void NativeParser::TokenStream::Advance() {
    ++pos_;
}

auto NativeParser::TokenStream::Size() const -> std::size_t {
    if (pos_ > tokens_.size()) {
        return 0;
    }
    return tokens_.size() - pos_;
}

auto NativeParser::TokenStream::Peek() const -> Token const& {
    if (Size() < 1) {
        throw std::out_of_range("No token available");
    }
    return tokens_.at(pos_);
}

auto NativeParser::TokenStream::PeekNext() const -> Token const& {
    if (Size() < 2) {
        throw std::out_of_range("No next token available");
    }
    return tokens_.at(pos_ + 1);
}

auto NativeParser::TokenStream::Pop() -> Token const& {
    auto const& tok = Peek();
    Advance();
    return tok;
}

auto NativeParser::TokenStream::PopCheck(TokenType token_type,
                                         char const* data) -> Token const& {
    auto const& token = Pop();
    if (token.type != token_type) {
        std::ostringstream stream;
        stream << "(" << token.line << ":" << token.column << ")"
               << "expecting token " << token_type << " but got " << token.type;
        throw ASTParseError(stream.str());
    }
    if (data != nullptr && token.value != data) {
        std::ostringstream stream;
        stream << "(" << token.line << ":" << token.column << ")"
               << "expecting value " << data << " but got " << token.value;
        throw ASTParseError(stream.str());
    }
    return token;
}

auto NativeParser::ParseData(const FileData& file_data)
    -> justlang::ASTNodePtr {
    try {
        auto const& filename = file_data.location.ToString();
        auto const& content = file_data.content;
        Lexer Lexer(filename, content);
        tokens_ = TokenStream{Lexer.Tokenize()};
        filename_ = filename;

        auto import_loc = file_data.location;
        auto import_it =
            std::find(import_chain_.begin(), import_chain_.end(), import_loc);
        if (import_it != import_chain_.end()) {
            // compute cycle graph and throw exception
            auto oss = std::ostringstream{};
            oss << ",-> " << import_it->ToString() << "\n";
            while (std::distance(++import_it, import_chain_.end()) > 1) {
                oss << "|   " << import_it->ToString() << "\n";
            }
            if (import_it == import_chain_.end()) {
                oss << "`---'\n";
            }
            else {
                oss << "`-- " << import_it->ToString() << "\n";
            }
            throw ParserException{"Detected cycle in import chain.\n" +
                                  oss.str()};
        }
        import_chain_.emplace_back(std::move(import_loc));
        // callback for reading imports
        auto expr = Parse(kMaxPriority);
        import_chain_.pop_back();
        return expr;
    } catch (std::exception const& e) {
        throw ASTParseError{e.what()};
    }
}

[[nodiscard]] auto NativeParser::Parse(unsigned priority) -> ASTNodePtr {
    if (auto ast = ParseLocalImport()) {
        return ast;
    }
    auto const begin = tokens_.Peek();
    ASTNodePtr const lhs = ParseSimple();
    return ParseInfix(lhs, begin, priority);
}

[[nodiscard]] auto NativeParser::ParseSimple() -> ASTNodePtr {
    Token tok = tokens_.Pop();
    switch (tok.type) {
        case TokenType::ASSERT:
        case TokenType::BRACE_R:
        case TokenType::BRACKET_R:
        case TokenType::COMMA:
        case TokenType::DOT:
        case TokenType::ELSE:
        case TokenType::ERROR:
        case TokenType::FOR:
        case TokenType::FUNCTION:
        case TokenType::IF:
        case TokenType::IN:
        case TokenType::IMPORT:
        case TokenType::PAREN_R:
        case TokenType::SEMICOLON:
        case TokenType::THEN:
            throw Unexpected(tok, "parsing terminal");

        // parse unary operator
        case TokenType::OPERATOR: {
            auto uop = OpIsUnary(tok.value);
            if (!uop) {
                throw ParserException(
                    "only support unary operator \"!\" and \"-\", but "
                    "got:" +
                        tok.value,
                    CreateLocation(tok, import_chain_.back()));
            }
            justlang::UnaryOperationNode::Type unary_type{};
            auto value_type = justlang::ValueType::Any;
            if (*uop == UnaryOp::Not) {
                unary_type = justlang::UnaryOperationNode::Type::Not;
                value_type = justlang::ValueType::Bool;
            }
            else if (*uop == UnaryOp::Minus) {
                unary_type = justlang::UnaryOperationNode::Type::Minus;
                value_type = justlang::ValueType::Number;
            }
            auto expr = Parse(kUnaryPriority);

            return std::make_shared<justlang::UnaryOperationNode>(
                CreateLocation(tok, expr, import_chain_.back()),
                unary_type,
                std::move(expr),
                value_type);
        }

        // begin of the description
        case TokenType::BRACE_L: {
            return ParseContent(tokens_.Peek());
        }

        case TokenType::BRACKET_L: {
            tok = tokens_.Peek();
            if (tok.type == TokenType::BRACKET_R) {
                tokens_.Advance();
                return std::make_shared<justlang::ListNode>(
                    CreateLocation(tok, import_chain_.back()),
                    justlang::ListNode::items_t{});
            }
            ASTNodePtr first = Parse(kMaxPriority);
            bool got_comma = false;
            tok = tokens_.Peek();
            if (!got_comma && tok.type == TokenType::COMMA) {
                tok = tokens_.Pop();
                got_comma = true;
            }

            if (tok.type == TokenType::FOR) {
                // It's a for comprehension
                tok = tokens_.Pop();
                Token id_tok = tokens_.PopCheck(TokenType::IDENTIFIER);
                tokens_.PopCheck(TokenType::IN);
                ASTNodePtr range = Parse(kMaxPriority);
                tokens_.PopCheck(TokenType::BRACKET_R);
                return std::make_shared<justlang::ForEachNode>(
                    CreateLocation(tok, tokens_.Peek(), import_chain_.back()),
                    std::move(id_tok.value),
                    std::move(range),
                    std::move(first));
            }

            // only elements
            justlang::ListNode::items_t items{};

            items.emplace_back(first);
            while (true) {
                tok = tokens_.Peek();
                // close bracket
                if (tok.type == TokenType::BRACKET_R) {
                    tokens_.Advance();
                    return std::make_shared<justlang::ListNode>(
                        CreateLocation(
                            tok, tokens_.Peek(), import_chain_.back()),
                        std::move(items));
                }
                if (!got_comma) {
                    throw ParserException(
                        "expected a comma before next array element.",
                        CreateLocation(tok, import_chain_.back()));
                }
                ASTNodePtr const expr = Parse(kMaxPriority);
                tok = tokens_.Peek();
                if (tok.type == TokenType::COMMA) {
                    got_comma = true;
                    tok = tokens_.Pop();
                }
                items.emplace_back(expr);
            }
        }

        case TokenType::PAREN_L: {
            // paren can appear in many situations
            // eg. f(a,b);jst.at("");
            // (1+2) *3
            // local f(a,b) = ....
            // import ("dir/"+ env + "....jst")
            // let other node to deal with it;
            auto body = Parse(kMaxPriority);
            tokens_.PopCheck(TokenType::PAREN_R);
            return body;
        }

        // Literals
        case TokenType::FLOAT:
        case TokenType::INTEGER:
        case TokenType::NUMBER:
            return std::make_shared<justlang::NumberNode>(
                CreateLocation(tok, import_chain_.back()),
                std::stod(tok.value));
        case TokenType::STRING_SINGLE:
        case TokenType::STRING_DOUBLE:
        case TokenType::TEXT_BLOCK:
            return std::make_shared<justlang::StringNode>(
                CreateLocation(tok, import_chain_.back()), tok.value);
        case TokenType::REF_STRING:
            return std::make_shared<justlang::RefNode>(
                CreateLocation(tok, import_chain_.back()),
                justlang::DecodeRefString(tok.value, false));
        case TokenType::VAR_STRING:
            return std::make_shared<justlang::VarNode>(
                CreateLocation(tok, import_chain_.back()), tok.value);
        case TokenType::FALSE:
            return std::make_shared<justlang::BoolNode>(
                CreateLocation(tok, import_chain_.back()), false);
        case TokenType::TRUE:
            return std::make_shared<justlang::BoolNode>(
                CreateLocation(tok, import_chain_.back()), true);
        case TokenType::NULL_LIT:
            return std::make_shared<justlang::NullNode>(
                CreateLocation(tok, import_chain_.back()));
        case TokenType::IDENTIFIER:
            return std::make_shared<justlang::CallNode>(
                CreateLocation(tok, import_chain_.back()), tok.value);

        default:
            return nullptr;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] auto NativeParser::ParseInfix(ASTNodePtr const& prev_node,
                                            Token const& curr_token,
                                            unsigned max_priority)
    -> ASTNodePtr {
    auto lhs = prev_node;
    auto next = curr_token;
    while (true) {
        auto begin = next;
        bool negate{};
        bool minus{};
        BinaryOp bop = BinaryOp::Plus;
        unsigned op_priority = 0;

        // in order to deal with situation:
        //      local foo = 'foo';
        //      local bar() = 'bar';
        //      foo + bar()
        // that no token left after bar() and we don't have {}.
        if (tokens_.Size() < 2) {
            return lhs;
        }

        switch (tokens_.Peek().type) {
            // Logical / arithmetic binary operator.
            case TokenType::IN:
            case TokenType::OPERATOR: {
                // These occur if the outer statement was an assert or array
                // slice. Either way, we terminate the parsing here.
                if (tokens_.Peek().value == ":" ||
                    tokens_.Peek().value == "::") {
                    return lhs;
                }
                auto found_bop = OpIsBinary(tokens_.Peek().value);
                if (not found_bop) {
                    std::ostringstream strs;
                    strs << "not a binary operator: " << tokens_.Peek().value;
                    throw ParserException(strs.str());
                }
                bop = *found_bop;
                op_priority = kPrecedenceMap.at(*found_bop);
            } break;

            // Index, Apply
            case TokenType::DOT:
            case TokenType::BRACKET_L:
            case TokenType::PAREN_L:
            case TokenType::BRACE_L:
                op_priority = kApplyPriority;
                break;

            default:
                // This happens when we reach EOF or the terminating token of an
                // outer context.
                return lhs;
        }
        if (bop == BinaryOp::Equal && tokens_.Peek().value == "!=") {
            negate = true;
        }
        if (bop == BinaryOp::Plus && tokens_.Peek().value == "-") {
            minus = true;
        }

        // If higher precedence than the outer recursive call, let outer handle;
        // eg: 1 + 2 * 3, when parseInfix(2, "*", 3), it returns 2 so that "*"
        // won't be consumed, let outer "+"" decide.
        if (op_priority >= max_priority) {
            return lhs;
        }

        next = tokens_.Pop();
        switch (next.type) {
            case TokenType::BRACKET_L: {
                ASTNodePtr target = nullptr;
                if (tokens_.Peek().type == TokenType::BRACE_R) {
                    throw ParserException(
                        "unexpected bracket_r when parsing index");
                }

                if (tokens_.Peek().value != ":" &&
                    tokens_.Peek().value != "::") {
                    target = Parse(kMaxPriority);
                }

                if ((tokens_.Peek().type == TokenType::OPERATOR &&
                     tokens_.Peek().value == "::") ||
                    tokens_.Peek().type != TokenType::BRACKET_R) {
                    throw ParserException("does not support slice");
                }
                tokens_.PopCheck(TokenType::BRACKET_R);  // end token check
                lhs = std::make_shared<justlang::LookupNode>(
                    CreateLocation(begin, tokens_.Peek(), import_chain_.back()),
                    target,
                    lhs);
                break;
            }
            case TokenType::DOT: {
                Token const field_id = tokens_.PopCheck(TokenType::IDENTIFIER);
                auto target = std::make_shared<justlang::StringNode>(
                    CreateLocation(field_id, import_chain_.back()),
                    field_id.value);

                lhs = std::make_shared<justlang::LookupNode>(
                    CreateLocation(begin, field_id, import_chain_.back()),
                    std::move(target),
                    lhs);

                break;
            }
            case TokenType::PAREN_L: {
                auto params = ParseCallArgs();
                if (tokens_.Size() >= 2) {
                    tokens_.PopCheck(TokenType::PAREN_R);
                }
                bool got_named = false;
                for (const auto& arg : params) {
                    if (arg.first.has_value()) {
                        got_named = true;
                    }
                    else {
                        if (got_named) {
                            throw ParserException(
                                "Positional argument after a named argument is "
                                "not allowed",
                                CreateLocation(tokens_.Peek(),
                                               import_chain_.back()));
                        }
                    }
                }

                if (auto derived =
                        std::dynamic_pointer_cast<justlang::CallNode>(lhs)) {
                    lhs = std::make_shared<justlang::CallNode>(
                        CreateLocation(
                            begin, tokens_.Peek(), import_chain_.back()),
                        derived->GetTarget(),
                        std::move(params));
                }
                else {
                    lhs = std::make_shared<justlang::CallNode>(
                        CreateLocation(
                            begin, tokens_.Peek(), import_chain_.back()),
                        lhs,
                        std::move(params));
                }
                break;
            }
            case TokenType::IN:
            case TokenType::OPERATOR: {
                auto const rhs = Parse(op_priority);
                auto const loc =
                    CreateLocation(begin, rhs, import_chain_.back());
                lhs = MakeOperationNode(loc, lhs, rhs, bop, negate, minus);
                break;
            }

            default:
                throw ParserException(
                    "should not be here",
                    CreateLocation(next, import_chain_.back()));
                return nullptr;
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
[[nodiscard]] auto NativeParser::ParseContent(const Token& begin)
    -> ASTNodePtr {
    auto fields = justlang::MapNode::fields_t{};
    // a field should followed by a comma,
    // a comma should followed by a filed
    enum class FieldState : std::uint8_t {
        FIRST_FIELD,
        EXPECTING_FIELD_OR_CLOSE,
        EXPECTING_COMMA_OR_CLOSE
    };
    std::unordered_set<std::string>
        seen_identifier;  // Local set to track seen identifiers
    FieldState field_state = FieldState::FIRST_FIELD;
    Token tok = begin;

    auto makeMapNode = [&](const Token& tok) {
        return std::make_shared<justlang::MapNode>(
            CreateLocation(tok, import_chain_.back()), std::move(fields));
    };

    auto parseFields =
        [this, &seen_identifier, &tok, &begin, &fields, &field_state]()
        -> void {
        switch (tok.type) {
            case TokenType::BRACKET_L:
            case TokenType::IDENTIFIER:
            case TokenType::STRING_SINGLE:
            case TokenType::STRING_DOUBLE:
            case TokenType::REF_STRING:
            case TokenType::VAR_STRING:
            case TokenType::TEXT_BLOCK: {
                std::shared_ptr<ASTNode> lhs = nullptr;
                Location const loc =
                    CreateLocation(begin, tok, import_chain_.back());
                // detecting duplicate identifier A, 'A-B'.

                if (tok.type == TokenType::IDENTIFIER ||
                    tok.type == TokenType::STRING_SINGLE ||
                    tok.type == TokenType::STRING_DOUBLE ||
                    tok.type == TokenType::TEXT_BLOCK) {
                    lhs =
                        std::make_shared<justlang::StringNode>(loc, tok.value);
                    if (!seen_identifier.insert(tok.value).second) {
                        throw ParserException("Duplicate identifier found",
                                              loc);
                    }
                    tokens_.Advance();  // skip lhs
                }
                else if (tok.type == TokenType::REF_STRING) {
                    lhs = std::make_shared<justlang::RefNode>(
                        loc, justlang::DecodeRefString(tok.value, false));
                    tokens_.Advance();  // skip lhs
                }
                else if (tok.type == TokenType::VAR_STRING) {
                    lhs = std::make_shared<justlang::VarNode>(loc, tok.value);
                    tokens_.Advance();  // skip lhs
                }
                else {
                    // it's bracket_L operation, expecting bracket_r after lhs
                    tokens_.Advance();
                    lhs = Parse(kMaxPriority);
                    tokens_.PopCheck(TokenType::BRACKET_R);
                }
                justlang::FuncNode::params_t params;
                bool isFunc{};
                if (tokens_.Peek().type == TokenType::PAREN_L) {
                    tokens_.Advance();  // pop paren_l
                    params = ParseFuncArgs();
                    isFunc = true;
                    tokens_.PopCheck(TokenType::PAREN_R);
                }
                tok = tokens_.PopCheck(TokenType::OPERATOR);
                if (tok.value != ":") {
                    throw ParserException(
                        "Expecting a colon after lhs.",
                        CreateLocation(tok, import_chain_.back()));
                }
                ASTNodePtr rhs = Parse(kMaxPriority);
                if (isFunc) {
                    rhs = std::make_shared<FuncNode>(loc, params, rhs);
                }
                tok = tokens_.Peek();  // update tok position after rhs
                auto entry = justlang::MapNode::entry_t{lhs, rhs};
                fields.emplace_back(entry);
                field_state = FieldState::EXPECTING_COMMA_OR_CLOSE;
            } break;

            default:
                throw Unexpected(tok, "parsing field definition");
        }
    };

    while (true) {
        tok = tokens_.Peek();
        if (tok.type == TokenType::BRACE_R) {
            if (tokens_.Size() > 1) {
                tokens_.Advance();
            }
            return makeMapNode(tok);
        }

        if (tok.type == TokenType::FOR) {
            // It's a for comprehension
            tok = tokens_.Pop();
            Token id_tok = tokens_.PopCheck(TokenType::IDENTIFIER);
            tokens_.PopCheck(TokenType::IN);
            ASTNodePtr range = Parse(kMaxPriority);
            tokens_.PopCheck(TokenType::BRACE_R);
            auto loc = CreateLocation(tok, range, import_chain_.back());
            auto make_map =
                std::make_shared<justlang::MapNode>(loc, std::move(fields));
            auto foreach =
                std::make_shared<justlang::ForEachNode>(loc,
                                                        std::move(id_tok.value),
                                                        std::move(range),
                                                        std::move(make_map));
            static auto const kUnionBuiltin =
                std::make_shared<justlang::BuiltinNode>("union");
            return std::make_shared<justlang::CallNode>(
                loc,
                kUnionBuiltin,
                justlang::CallNode::params_t{{"maps", std::move(foreach)}});
        }

        if (field_state == FieldState::EXPECTING_COMMA_OR_CLOSE) {
            throw ParserException("expected a comma before next field.",
                                  CreateLocation(tok, import_chain_.back()));
        }

        parseFields();
        field_state = (tok.type == TokenType::COMMA)
                          ? FieldState::EXPECTING_FIELD_OR_CLOSE
                          : field_state;

        if (field_state == FieldState::EXPECTING_FIELD_OR_CLOSE) {
            tokens_.Advance();  // pop out the comma in between fields
        }
    }
}

[[nodiscard]] auto NativeParser::ParseLocalImport() -> ASTNodePtr {  // NOLINT
    std::unique_ptr<Token> const begin_(new Token(tokens_.Peek()));
    const Token& begin = *begin_;
    std::unordered_set<std::string>
        seen_identifier;  // Local set to track seen identifiers
    switch (begin.type) {
        case TokenType::LOCAL: {
            tokens_.Advance();  // pop keyword: local
            const Token idf = tokens_.PopCheck(TokenType::IDENTIFIER);
            if (!seen_identifier.insert(idf.value).second) {
                throw ParserException(
                    "Duplicate identifier found",
                    CreateLocation(idf, import_chain_.back()));
            }

            ASTNodePtr body;
            justlang::FuncNode::params_t params;
            bool const isFunction = tokens_.Peek().type == TokenType::PAREN_L;

            if (isFunction) {
                tokens_.Advance();  // skip "("
                params = ParseFuncArgs();
                tokens_.Advance();  // skip ")"
                tokens_.PopCheck(TokenType::OPERATOR, "=");
                body = Parse(kMaxPriority);
                tokens_.PopCheck(TokenType::SEMICOLON);  // close the funcnode
            }
            else if (tokens_.Peek().type == TokenType::OPERATOR &&
                     tokens_.Peek().value == "=") {
                tokens_.Advance();  // skip "="
                body = Parse(kMaxPriority);
                // advance();  //   skip "}"
                tokens_.PopCheck(TokenType::SEMICOLON);
            }
            else {
                throw ParserException(
                    "Expecting PAREN_L or = but got",
                    CreateLocation(tokens_.Peek(), import_chain_.back()));
            }
            auto loc = CreateLocation(begin, body, import_chain_.back());
            auto next = Parse(kMaxPriority);
            ASTNodePtr value =
                isFunction ? std::make_shared<FuncNode>(loc, params, body)
                           : body;
            return std::make_shared<justlang::LetNode>(
                loc, idf.value, std::move(value), std::move(next));
        }

        case TokenType::IMPORT: {
            tokens_.Advance();  // skip import
            auto tok = tokens_.Pop();
            auto file = std::string{};
            auto repo = std::optional<std::string>{};
            switch (tok.type) {
                case TokenType::STRING_SINGLE:
                case TokenType::STRING_DOUBLE:
                    file = tok.value;
                    break;
                case TokenType::REF_STRING: {
#ifdef SUPPORT_IMPORT_VIA_EXT_REF
                    // encoded file-reference: @'<repo>//<file>'
                    auto const& data =
                        DecodeRefString(tok.value, /*file_ref=*/true);
                    if (data.type != justlang::RefType::Ext) {
                        throw ParserException{
                            "Missing repository for import from external "
                            "file-reference @'<repo>//<file_path>'."};
                    }
                    file = data.module;
                    repo = data.repo;
#else
                    throw ParserException{
                        "Imports from ref-strings are not supported."};
#endif
                } break;
                default: {
#ifdef SUPPORT_IMPORT_VIA_EXT_REF
                    throw ParserException{
                        "Only imports from plain path or ref-strings are "
                        "supported."};
#else
                    throw ParserException{
                        "Only imports from plain path are supported."};
#endif
                } break;
            }

            if (auto data = reader_(
                    import_chain_.back(), file, repo ? &(*repo) : nullptr)) {
                auto import_parser =
                    std::make_unique<NativeParser>(reader_, import_chain_);
                return std::make_shared<justlang::ForeignNode>(
                    CreateLocation(begin, tokens_.Peek(), import_chain_.back()),
                    import_parser->ParseData(data.value()));
            }

            throw ParserException("Failed to import file");
        }

        case TokenType::FUNCTION: {
            tokens_.Advance();
            auto next = tokens_.Pop();
            if (next.type == TokenType::PAREN_L) {
                auto params = ParseFuncArgs();
                tokens_.Advance();  // skip ")"
                ASTNodePtr const body = Parse(kMaxPriority);
                return std::make_shared<justlang::FuncNode>(
                    CreateLocation(begin, body, import_chain_.back()),
                    std::move(params),
                    body);
            }
            throw ParserException("Expecting a PAREN_L instead.",
                                  CreateLocation(next, import_chain_.back()));
        }

        case TokenType::IF: {
            tokens_.Advance();
            ASTNodePtr cond = Parse(kMaxPriority);
            tokens_.PopCheck(TokenType::THEN);
            ASTNodePtr branch_true = Parse(kMaxPriority);
            if (tokens_.Peek().type == TokenType::ELSE) {
                tokens_.Advance();  // pop else
                ASTNodePtr branch_false = Parse(kMaxPriority);
                return std::make_shared<justlang::IfNode>(
                    CreateLocation(begin, branch_false, import_chain_.back()),
                    std::move(cond),
                    std::move(branch_true),
                    std::move(branch_false));
            }
            return std::make_shared<justlang::IfNode>(
                CreateLocation(begin, branch_true, import_chain_.back()),
                std::move(cond),
                std::move(branch_true),
                ASTNodePtr{});
        }

        default:
            return nullptr;
    }
}

// Parse a comma-separated list of expressions.
template <class TParams>
    requires(std::same_as<TParams, FuncNode::params_t> ||
             std::same_as<TParams, CallNode::params_t>)
[[nodiscard]] auto NativeParser::ParseArgs() -> TParams {
    TParams params{};

    bool got_comma = false;
    bool first = true;
    while (true) {
        Token const next = tokens_.Peek();
        if (next.type == TokenType::PAREN_R) {
            return params;
        }
        if (!first && !got_comma) {
            throw ParserException("expect a comma before next field",
                                  CreateLocation(next, import_chain_.back()));
        }

        auto name = std::optional<std::string>{};
        auto temp_name = std::string{};
        ASTNodePtr expr = nullptr;
        bool complex_arg = false;
        if (next.type == TokenType::IDENTIFIER ||
            next.type == TokenType::STRING_SINGLE ||
            next.type == TokenType::STRING_DOUBLE) {
            Token const maybe_eq = tokens_.PeekNext();
            temp_name = tokens_.Peek().value;
            if (maybe_eq.type == TokenType::OPERATOR && maybe_eq.value == "=") {
                name = tokens_.Peek().value;
                tokens_.Advance();  // id
                tokens_.Advance();  // eq
                complex_arg = true;
            }
        }

        expr = Parse(kMaxPriority);
        got_comma = false;
        first = false;
        if (tokens_.Peek().type == TokenType::COMMA) {
            got_comma = true;
            tokens_.Advance();
        }

        if constexpr (std::is_same_v<TParams, FuncNode::params_t>) {
            if (complex_arg) {
                // with default arguments
                params.emplace_back(std::move(*name), std::move(expr));
            }
            else {
                params.emplace_back(std::move(temp_name), ASTNodePtr{});
            }
        }
        else {
            params.emplace_back(std::move(name), std::move(expr));
        }
    }
}

[[nodiscard]] auto NativeParser::ParseFuncArgs() -> FuncNode::params_t {
    return ParseArgs<FuncNode::params_t>();
}

[[nodiscard]] auto NativeParser::ParseCallArgs() -> CallNode::params_t {
    return ParseArgs<CallNode::params_t>();
}

auto Parser::Create(FileData::reader_t reader) -> justlang::ParserPtr {
    return std::make_unique<NativeParser>(std::move(reader),
                                          std::vector<FileLocation>{});
}

}  // namespace justlang
