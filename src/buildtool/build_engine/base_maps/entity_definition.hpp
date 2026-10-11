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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_DEFINITION_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_DEFINITION_HPP

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/field_expression.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"
#include "src/utils/cpp/expected.hpp"

namespace BuildMaps::Base {

/// \brief Definition of an entity (a target or a rule): either a JSON object
/// or the (lazily evaluated) fields of a jstlang definition.
class EntityDefinition final {
  public:
    using Logger = std::function<void(std::string const&)>;

    explicit EntityDefinition(nlohmann::json json) noexcept
        : data_{std::move(json)} {}

    explicit EntityDefinition(JstlangEvaluator::Fields fields) noexcept
        : data_{std::move(fields)} {}

    /// \brief Whether the description is a map of fields.
    [[nodiscard]] auto IsObject() const noexcept -> bool {
        auto const* json = std::get_if<nlohmann::json>(&data_);
        return json == nullptr or json->is_object();
    }

    [[nodiscard]] auto FieldNames() const -> std::vector<std::string> {
        auto names = std::vector<std::string>{};
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            for (auto const& [key, _] : json->items()) {
                names.emplace_back(key);
            }
        }
        else {
            for (auto const& [key, _] : std::get<Fields>(data_)) {
                names.emplace_back(key);
            }
        }
        return names;
    }

    [[nodiscard]] auto Contains(std::string const& name) const -> bool {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            return json->contains(name);
        }
        return std::get<Fields>(data_).contains(name);
    }

    /// \brief Read the plain JSON value of a field. Fields of jstlang
    /// definitions are evaluated statically (without configuration).
    /// \returns nullopt if field is missing; unexpected on error (reported via
    /// logger).
    [[nodiscard]] auto ReadJson(std::string const& name, Logger const& logger)
        const -> expected<std::optional<nlohmann::json>, std::monostate> {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            auto it = json->find(name);
            if (it == json->end()) {
                return std::optional<nlohmann::json>{};
            }
            return std::optional{*it};
        }
        auto const& fields = std::get<Fields>(data_);
        auto it = fields.find(name);
        if (it == fields.end()) {
            return std::optional<nlohmann::json>{};
        }
        auto value = JstlangEvaluator::Evaluate(
            it->second,
            /*env=*/nullptr,
            [&logger, &name](std::string const& msg) {
                logger("While statically evaluating field " + name + ":\n" +
                       msg);
            });
        if (not value) {
            return unexpected{std::monostate{}};
        }
        return std::optional{value->ToJson()};
    }

    /// \brief Read field as expression for evaluation.
    /// \returns nullopt if field is missing; empty field expression if
    /// creating the expression failed.
    [[nodiscard]] auto ReadFieldExpression(std::string const& name) const
        -> std::optional<FieldExpression> {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            auto it = json->find(name);
            if (it == json->end()) {
                return std::nullopt;
            }
            return FieldExpression{Expression::FromJson(*it)};
        }
        auto const& fields = std::get<Fields>(data_);
        auto it = fields.find(name);
        if (it == fields.end()) {
            return std::nullopt;
        }
        return FieldExpression{it->second};
    }

    /// \brief Read map field as expressions (one per entry) for evaluation.
    /// For JSON descriptions, the field must be a literal map; for jstlang
    /// definitions, the field is evaluated statically to a map, without
    /// evaluating its values.
    /// \returns nullopt if field is missing; unexpected on error (reported via
    /// logger).
    [[nodiscard]] auto ReadFieldExpressions(std::string const& name,
                                            Logger const& logger) const
        -> expected<std::optional<std::map<std::string, FieldExpression>>,
                    std::monostate> {
        using map_t = std::map<std::string, FieldExpression>;
        auto result = map_t{};
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            auto it = json->find(name);
            if (it == json->end()) {
                return std::optional<map_t>{};
            }
            if (not it->is_object()) {
                logger("Expected field " + name + " to be a map, but found " +
                       it->dump());
                return unexpected{std::monostate{}};
            }
            for (auto const& [key, value] : it->items()) {
                auto expr = Expression::FromJson(value);
                if (not expr) {
                    logger("Failed to create expression from JSON:\n  " +
                           value.dump());
                    return unexpected{std::monostate{}};
                }
                result.emplace(key, FieldExpression{std::move(expr)});
            }
            return std::optional{std::move(result)};
        }
        auto const& fields = std::get<Fields>(data_);
        auto it = fields.find(name);
        if (it == fields.end()) {
            return std::optional<map_t>{};
        }
        auto entries = JstlangEvaluator::EvaluateFields(
            it->second, [&logger, &name](std::string const& msg) {
                logger("While statically evaluating field " + name + ":\n" +
                       msg);
            });
        if (not entries) {
            return unexpected{std::monostate{}};
        }
        for (auto& [key, field] : *entries) {
            result.emplace(key, FieldExpression{std::move(field)});
        }
        return std::optional{std::move(result)};
    }

    /// \brief Representation as JSON, e.g., for displaying the description.
    /// Fields of jstlang definitions are evaluated statically; fields that
    /// cannot be evaluated statically are omitted.
    [[nodiscard]] auto ToJson() const -> nlohmann::json {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            return *json;
        }
        auto result = nlohmann::json::object();
        for (auto const& name : FieldNames()) {
            if (auto value = ReadJson(name, [](auto const& /*unused*/) {});
                value and *value) {
                result[name] = **value;
            }
        }
        return result;
    }

  private:
    using Fields = JstlangEvaluator::Fields;
    std::variant<nlohmann::json, Fields> data_;
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITY_DEFINITION_HPP
