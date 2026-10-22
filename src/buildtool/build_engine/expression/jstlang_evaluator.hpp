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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_JSTLANG_EVALUATOR_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_JSTLANG_EVALUATOR_HPP

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/jstlang/evaluator.hpp"
#include "src/buildtool/jstlang/file_data.hpp"

/// \brief Evaluation of jstlang ASTs for the build engine, based on the
/// evaluator of jstlang (full evaluation). Values are converted to and from
/// values of the expression language at the boundary (configuration, result).
class JstlangEvaluator final {
  public:
    using Logger = std::function<void(std::string const&)>;
    using Annotator = std::function<std::string(ExpressionPtr)>;
    using NoteUserContext = std::function<void(void)>;

    /// \brief Lazily evaluated field of a map (e.g., of a target definition):
    /// a path of entries in the value of an expression.
    class Field final {
      public:
        Field(jstlang::ASTNodePtr expr,
              jstlang::Bindings::Ptr bindings,
              std::vector<std::string> path,
              jstlang::ASTNodePtr static_value,
              bool allow_unfold) noexcept
            : expr_{std::move(expr)},
              bindings_{std::move(bindings)},
              path_{std::move(path)},
              static_value_{std::move(static_value)},
              allow_unfold_{allow_unfold} {}

        /// \brief Expression containing the field.
        [[nodiscard]] auto Expr() const noexcept -> jstlang::ASTNodePtr const& {
            return expr_;
        }

        [[nodiscard]] auto GetBindings() const noexcept
            -> jstlang::Bindings::Ptr const& {
            return bindings_;
        }

        /// \brief Path of entries selecting the field in the expression's
        /// value (empty: the field is the value of the expression).
        [[nodiscard]] auto Path() const noexcept
            -> std::vector<std::string> const& {
            return path_;
        }

        /// \brief Value without configuration: a literal, or the
        /// self-contained residual code of unfolding (evaluated in a
        /// configuration directly); nullptr if not known.
        [[nodiscard]] auto StaticValue() const noexcept
            -> jstlang::ASTNodePtr const& {
            return static_value_;
        }

        /// \brief Whether maps of this field that are not literal maps may
        /// be unfolded to obtain their structure (see EvaluateFields()).
        [[nodiscard]] auto AllowUnfold() const noexcept -> bool {
            return allow_unfold_;
        }

      private:
        jstlang::ASTNodePtr expr_;
        jstlang::Bindings::Ptr bindings_;
        std::vector<std::string> path_;
        jstlang::ASTNodePtr static_value_;
        bool allow_unfold_;
    };
    using FieldPtr = std::shared_ptr<Field const>;
    using Fields = std::map<std::string, FieldPtr>;

    /// \brief Evaluate expression to a value of the expression language.
    /// \param expr       The expression to evaluate.
    /// \param bindings   The bindings to evaluate the expression in.
    /// \param env        The configuration for reading runtime variables. If
    ///                   nullptr, reading runtime variables is an error
    ///                   (static evaluation).
    /// \param functions  The functions of the rule analysis (e.g., FIELD or
    ///                   ACTION) for evaluating rule-only built-in functions.
    ///                   If nullptr, those cannot be evaluated.
    /// \returns the value or nullptr on error (reported via logger).
    [[nodiscard]] static auto Evaluate(
        jstlang::ASTNodePtr const& expr,
        jstlang::Bindings::Ptr const& bindings,
        Configuration const* env,
        FunctionMapPtr const& functions,
        Logger const& logger,
        Annotator const& annotate_object =
            [](auto const& /*unused*/) { return std::string{}; },
        NoteUserContext const& note_user_context =
            []() {}) noexcept -> ExpressionPtr;

    /// \brief Parse and evaluate a file, with its imports (from the same
    /// repository) read via the reader.
    /// \param content  The content of the file; if not given, the file is
    ///                 read via the reader as well.
    /// \returns the value or nullptr on error (reported via logger).
    [[nodiscard]] static auto EvaluateFile(
        jstlang::FileReader const& reader,
        jstlang::FileLocation const& location,
        std::optional<std::string> const& content,
        Configuration const& env,
        Logger const& logger) noexcept -> ExpressionPtr;

    /// \brief Evaluate value of field, see Evaluate() for parameters.
    [[nodiscard]] static auto Evaluate(
        FieldPtr const& field,
        Configuration const* env,
        FunctionMapPtr const& functions,
        Logger const& logger,
        Annotator const& annotate_object =
            [](auto const& /*unused*/) { return std::string{}; },
        NoteUserContext const& note_user_context =
            []() {}) noexcept -> ExpressionPtr;

    /// \brief Statically evaluate expression to a map, without evaluating
    /// the values of its fields (their values may depend on the
    /// configuration, the structure of the map must not). The fields of a
    /// literal map are evaluated separately. Other expressions are unfolded
    /// to obtain the map's structure, if allowed (otherwise, the expression
    /// must be a literal map); fields whose unfolded values are
    /// self-contained are evaluated from those.
    /// \returns the map's fields or nullopt on error (reported via logger).
    [[nodiscard]] static auto EvaluateFields(
        jstlang::ASTNodePtr const& expr,
        jstlang::Bindings::Ptr const& bindings,
        bool allow_unfold,
        Logger const& logger) noexcept -> std::optional<Fields>;

    /// \brief Statically evaluate value of field to a map, without evaluating
    /// the values of its fields.
    /// \returns the map's fields or nullopt on error (reported via logger).
    [[nodiscard]] static auto EvaluateFields(FieldPtr const& field,
                                             Logger const& logger) noexcept
        -> std::optional<Fields>;
};

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_EXPRESSION_JSTLANG_EVALUATOR_HPP
