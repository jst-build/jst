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

#include "src/buildtool/jstlang/preprocessor.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/analyze.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/augment.hpp"
#include "src/buildtool/jstlang/ast/builtin_functions.hpp"
#include "src/buildtool/jstlang/ast/inline.hpp"
#include "src/buildtool/jstlang/ast/parser.hpp"
#include "src/buildtool/jstlang/ast/to_json_visitor.hpp"
#ifndef NDEBUG
#include "src/buildtool/jstlang/ast/to_string_visitor.hpp"
#endif
#include "src/buildtool/jstlang/file_data.hpp"

auto jstlang::Preprocessor::Process(std::string const& global_repo,
                                    std::filesystem::path const& file_path,
                                    jstlang::FileType type) noexcept
    -> jstlang::ASTNodePtr {
    auto root = jstlang::FileLocation{.repo = global_repo, .path = "/"};
    auto data = reader_(root, file_path, nullptr);
    if (not data) {
        root.path = file_path;
        logger_(LogType::Error,
                "Reading file " + root.ToString() + " failed.\n");
        return nullptr;
    }

    return Process(*data, type);
}

auto jstlang::Preprocessor::Process(jstlang::FileData const& file_data,
                                    jstlang::FileType type) noexcept
    -> jstlang::ASTNodePtr {
    auto err_msg = std::ostringstream{};
    auto parser = Parser::Create(reader_);
    try {
        auto jst_ast = parser->ParseData(file_data);

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Jstlang AST\n" + ASTToStringVisitor{}.Dump(*jst_ast));
#endif

        if (type != jstlang::FileType::Plain) {
            jstlang::StaticAnalysis(jst_ast);
        }

        // inject built-ins using namespace 'jst'
        jst_ast = ProvideBuiltinsToAST(jst_ast);

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Jstlang AST with builtins\n" +
                    ASTToStringVisitor{}.Dump(*jst_ast));
#endif

        auto inl_ast = jstlang::InlineAST(jst_ast);

        switch (type) {
            case jstlang::FileType::Plain:
                // nothing to do, no augmentation needed
                break;
            case jstlang::FileType::Targets:
                inl_ast = jstlang::AugmentAST(inl_ast);
                break;
            case jstlang::FileType::Rules:
            case jstlang::FileType::Expressions:
                logger_(LogType::Error, "not yet implemented file type");
                return nullptr;
        }

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Inlined and augmented Jstlang AST\n" +
                    ASTToStringVisitor{}.Dump(*inl_ast));
#endif

        return inl_ast;

    } catch (jstlang::ASTParseError const& e) {
        err_msg << e.what() << "\n";
    } catch (jstlang::ASTInlineError const& e) {
        err_msg << "AST inlining failed with:\n" << e.what() << "\n";
    } catch (jstlang::ASTAugmentError const& e) {
        err_msg << "AST augmentation failed with:\n" << e.what() << "\n";
    } catch (std::exception const& e) {
        err_msg << "Internal error:\n" << e.what() << "\n";
    }

    logger_(LogType::Error, err_msg.str());

    return nullptr;
}

auto jstlang::Preprocessor::Serialize(jstlang::ASTNodePtr const& ast) noexcept
    -> std::optional<nlohmann::json> {
    try {
        if (ast) {
            return jstlang::ASTToJsonVisitor{}.ToJson(*ast);
        }
    } catch (const std::exception& e) {
        logger_(LogType::Error,
                std::string{"AST serialization error:\n"} + e.what());
    }
    return std::nullopt;
}
