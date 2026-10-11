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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_EXPRESSION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_EXPRESSION_HPP

#include <functional>
#include <string>
#include <utility>

#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/build_engine/expression/function_map.hpp"
#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"

namespace BuildMaps::Base {

/// \brief Expression of a field of an entity definition (e.g., a target
/// definition) for evaluation in a given configuration. It is either an
/// expression of the expression language (e.g., from a JSON definition) or a
/// field of a jstlang definition, which is evaluated by the runtime evaluator.
class FieldExpression final {
  public:
    FieldExpression() noexcept = default;

    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    FieldExpression(ExpressionPtr expr) noexcept : expr_{std::move(expr)} {}

    explicit FieldExpression(JstlangEvaluator::FieldPtr field) noexcept
        : field_{std::move(field)} {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return static_cast<bool>(field_) or static_cast<bool>(expr_);
    }

    /// \brief The plain expression, nullptr for fields of jstlang definitions.
    [[nodiscard]] auto Plain() const noexcept -> ExpressionPtr const& {
        return expr_;
    }

    /// \brief Evaluate the field, see ExpressionPtr::Evaluate().
    [[nodiscard]] auto Evaluate(
        Configuration const& env,
        FunctionMapPtr const& functions,
        std::function<void(std::string const&)> const& logger =
            [](std::string const& error) noexcept -> void {
            Logger::Log(LogLevel::Error, error);
        },
        std::function<std::string(ExpressionPtr)> const& annotate_object =
            [](auto const& /*unused*/) { return std::string{}; },
        std::function<void(void)> const& note_user_context =
            []() noexcept -> void {}) const noexcept -> ExpressionPtr {
        if (field_) {
            return JstlangEvaluator::Evaluate(field_, &env, logger);
        }
        return expr_.Evaluate(
            env, functions, logger, annotate_object, note_user_context);
    }

  private:
    ExpressionPtr expr_{nullptr};
    JstlangEvaluator::FieldPtr field_;
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_EXPRESSION_HPP
