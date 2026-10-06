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

#include "src/buildtool/jstlang/preproc/preprocessor.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>

#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/analyze.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/ast/builtin_functions.hpp"
#include "src/buildtool/jstlang/ast/eval_visitor.hpp"
#include "src/buildtool/jstlang/ast/parser.hpp"
#ifndef NDEBUG
#include "src/buildtool/jstlang/ast/to_string_visitor.hpp"
#endif
#include "src/buildtool/jstlang/file_data.hpp"

auto jstlang::Preprocessor::Process(gsl::not_null<ASTContext*> const& context,
                                    std::string const& global_repo,
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

    return Process(context, *data, type);
}

auto jstlang::Preprocessor::Process(gsl::not_null<ASTContext*> const& context,
                                    jstlang::FileData const& file_data,
                                    jstlang::FileType type) noexcept
    -> jstlang::ASTNodePtr {
    auto err_msg = std::ostringstream{};
    auto parser = Parser::Create(context, reader_);
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

        // inline all functions and variables
        auto scope = ScopeDefs{};
        auto inl_ast =
            ASTEvalVisitor{&scope, /*partial_inline=*/false}.Evaluate(jst_ast);

#ifndef NDEBUG
        logger_(
            LogType::Debug,
            "---- Inlined Jstlang AST\n" + ASTToStringVisitor{}.Dump(*inl_ast));
#endif

        return inl_ast;

    } catch (jstlang::ASTParseError const& e) {
        err_msg << e.what() << "\n";
    } catch (jstlang::ASTEvalError const& e) {
        err_msg << "AST inlining failed with:\n" << e.what() << "\n";
    } catch (std::exception const& e) {
        err_msg << "Internal error:\n" << e.what() << "\n";
    }

    logger_(LogType::Error, err_msg.str());

    return nullptr;
}

auto jstlang::Preprocessor::Serialize(jstlang::ASTNodePtr const& ast,
                                      jstlang::FileType type) noexcept
    -> std::optional<nlohmann::json> {
    try {
        if (ast) {
            return jstlang::ExprSerializer{type}.Serialize(*ast);
        }
    } catch (jstlang::ExprSerializerError const& e) {
        logger_(LogType::Error,
                std::string{"AST structure error:\n"} + e.what());
    } catch (const std::exception& e) {
        logger_(LogType::Error,
                std::string{"AST serialization error:\n"} + e.what());
    }
    return std::nullopt;
}
