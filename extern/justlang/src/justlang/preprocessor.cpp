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

#include "justlang/preprocessor.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/analyze.hpp"
#include "justlang/ast/ast.hpp"
#include "justlang/ast/augment.hpp"
#include "justlang/ast/builtin_functions.hpp"
#include "justlang/ast/inline.hpp"
#include "justlang/ast/parser.hpp"
#include "justlang/ast/to_json_visitor.hpp"
#ifndef NDEBUG
#include "justlang/ast/to_string_visitor.hpp"
#endif
#include "justlang/file_data.hpp"

auto justlang::Preprocessor::Process(std::string const& global_repo,
                                     std::filesystem::path const& file_path,
                                     justlang::FileType type) noexcept
    -> justlang::ASTNodePtr {
    auto root = justlang::FileLocation{.repo = global_repo, .path = "/"};
    auto data = reader_(root, file_path, nullptr);
    if (not data) {
        root.path = file_path;
        logger_(LogType::Error,
                "Reading file " + root.ToString() + " failed.\n");
        return nullptr;
    }

    return Process(*data, type);
}

auto justlang::Preprocessor::Process(justlang::FileData const& file_data,
                                     justlang::FileType type) noexcept
    -> justlang::ASTNodePtr {
    auto err_msg = std::ostringstream{};
    auto parser = Parser::Create(reader_);
    try {
        auto just_ast = parser->ParseData(file_data);

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Justlang AST\n" + ASTToStringVisitor{}.Dump(*just_ast));
#endif

        if (type != justlang::FileType::Plain) {
            justlang::StaticAnalysis(just_ast);
        }

        // inject built-ins using namespace 'jst'
        just_ast = ProvideBuiltinsToAST(just_ast);

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Justlang AST with builtins\n" +
                    ASTToStringVisitor{}.Dump(*just_ast));
#endif

        auto inl_ast = justlang::InlineAST(just_ast);

        switch (type) {
            case justlang::FileType::Plain:
                // nothing to do, no augmentation needed
                break;
            case justlang::FileType::Targets:
                inl_ast = justlang::JustAugmentAST(inl_ast);
                break;
            case justlang::FileType::Rules:
            case justlang::FileType::Expressions:
                logger_(LogType::Error, "not yet implemented file type");
                return nullptr;
        }

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Inlined and augmented Justlang AST\n" +
                    ASTToStringVisitor{}.Dump(*inl_ast));
#endif

        return inl_ast;

    } catch (justlang::ASTParseError const& e) {
        err_msg << e.what() << "\n";
    } catch (justlang::ASTInlineError const& e) {
        err_msg << "AST inlining failed with:\n" << e.what() << "\n";
    } catch (justlang::ASTAugmentError const& e) {
        err_msg << "AST augmentation failed with:\n" << e.what() << "\n";
    } catch (std::exception const& e) {
        err_msg << "Internal error:\n" << e.what() << "\n";
    }

    logger_(LogType::Error, err_msg.str());

    return nullptr;
}

auto justlang::Preprocessor::Serialize(justlang::ASTNodePtr const& ast) noexcept
    -> std::optional<nlohmann::json> {
    try {
        if (ast) {
            return justlang::ASTToJsonVisitor{}.ToJson(*ast);
        }
    } catch (const std::exception& e) {
        logger_(LogType::Error,
                std::string{"AST serialization error:\n"} + e.what());
    }
    return std::nullopt;
}
