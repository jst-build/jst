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

#include "justlang/ast/parser.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <ast.h>           // jsonnet
#include <lexer.h>         // jsonnet
#include <parser.h>        // jsonnet
#include <static_error.h>  // jsonnet

#include "justlang/ast/ast.hpp"
#include "justlang/ast/jsonnet/translate.hpp"
#include "justlang/file_data.hpp"

namespace {

class ParserJsonnet final : public justlang::Parser {
  public:
    explicit ParserJsonnet(justlang::FileData::reader_t reader)
        : reader_{std::move(reader)} {}
    ParserJsonnet(ParserJsonnet const&) = delete;
    ParserJsonnet(ParserJsonnet&&) = delete;
    ~ParserJsonnet() final = default;

    auto operator=(ParserJsonnet const&) = delete;
    auto operator=(ParserJsonnet&&) = delete;

    [[nodiscard]] auto ParseData(justlang::FileData const& file_data)
        -> justlang::ASTNodePtr final {
        try {
            auto* expr = ParseJsonnetAST(file_data);

            // callback for reading imports
            auto callback = [this](justlang::FileLocation const& imported_from,
                                   std::string const& file_path,
                                   std::string const* repo)
                -> std::optional<justlang::jsonnet::AstData> {
                if (auto data = this->reader_(imported_from, file_path, repo)) {
                    return justlang::jsonnet::AstData{
                        .origin = data->location,
                        .ast = this->ParseJsonnetAST(*data)};
                }
                return std::nullopt;
            };

            return justlang::jsonnet::TranslateToJust(
                expr, file_data.location, std::move(callback));
        } catch (::jsonnet::internal::StaticError const& e) {
            throw justlang::ASTParseError{"Parser error: " + e.toString()};
        } catch (justlang::jsonnet::ASTTranslateError const& e) {
            throw justlang::ASTParseError{
                std::string{"AST translation failed with:\n"} + e.what()};
        }
    }

  private:
    ::jsonnet::internal::Allocator alloc_;
    justlang::FileData::reader_t reader_;

    [[nodiscard]] auto ParseJsonnetAST(justlang::FileData const& file_data)
        -> ::jsonnet::internal::AST* {
        auto const& filename = file_data.location.ToString();
        auto const& content = file_data.content;
        auto tokens =
            ::jsonnet::internal::jsonnet_lex(filename, content.c_str());
        return ::jsonnet::internal::jsonnet_parse(&alloc_, tokens);
    }
};

}  // namespace

auto justlang::Parser::Create(FileData::reader_t reader)
    -> justlang::ParserPtr {
    return std::make_unique<ParserJsonnet>(std::move(reader));
}
