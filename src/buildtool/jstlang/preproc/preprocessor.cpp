// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
// Copyright 2026 The jst-build authors.
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
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "nlohmann/json.hpp"  // IWYU pragma: keep
#include "nlohmann/json_fwd.hpp"
#include "src/buildtool/jstlang/ast/analyze.hpp"
#include "src/buildtool/jstlang/ast/ast.hpp"
#include "src/buildtool/jstlang/evaluator.hpp"
#include "src/buildtool/jstlang/parser.hpp"
#ifndef NDEBUG
#include "src/buildtool/jstlang/ast/to_string_visitor.hpp"
#endif
#include "src/buildtool/jstlang/file_data.hpp"

auto jstlang::Preprocessor::Process(gsl::not_null<ASTContext*> const& context,
                                    FileLocation const& location,
                                    jstlang::FileType type) noexcept
    -> jstlang::ASTNodePtr {
    return ProcessWith(
        context,
        [&location](Parser& parser) { return parser.ParseFile(location); },
        type);
}

auto jstlang::Preprocessor::Process(gsl::not_null<ASTContext*> const& context,
                                    FileLocation location,
                                    std::string content,
                                    jstlang::FileType type) noexcept
    -> jstlang::ASTNodePtr {
    return ProcessWith(
        context,
        [&location, &content](Parser& parser) {
            return parser.ParseData(std::move(location), std::move(content));
        },
        type);
}

auto jstlang::Preprocessor::ProcessWith(
    gsl::not_null<ASTContext*> const& context,
    std::function<ASTNodePtr(Parser&)> const& parse,
    jstlang::FileType type) noexcept -> jstlang::ASTNodePtr {
    auto err_msg = std::ostringstream{};
    try {
        auto parser = Parser{context, reader_, /*defer_imports=*/false, repos_};
        auto jst_ast = parse(parser);

#ifndef NDEBUG
        logger_(LogType::Debug,
                "---- Jstlang AST\n" + ASTToStringVisitor{}.Dump(*jst_ast));
#endif

        if (type != jstlang::FileType::Plain) {
            jstlang::StaticAnalysis(jst_ast);
        }

        // Lower AST to code of the expression language (lowering mode of the
        // evaluator: all functions are unfolded).
        auto inl_ast =
            Evaluator{EvalOptions{.mode = EvalOptions::Mode::Lowering}}
                .EvaluateAST(jst_ast);

#ifndef NDEBUG
        logger_(
            LogType::Debug,
            "---- Inlined Jstlang AST\n" + ASTToStringVisitor{}.Dump(*inl_ast));
#endif

        return inl_ast;

    } catch (jstlang::ASTParseError const& e) {
        err_msg << e.what() << "\n";
    } catch (jstlang::ASTEvalError const& e) {
        err_msg << "Lowering failed with:\n" << e.what() << "\n";
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
