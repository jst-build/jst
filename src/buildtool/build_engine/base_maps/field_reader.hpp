// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_READER_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_READER_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>  // std::move
#include <variant>
#include <vector>

#include "fmt/format.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_definition.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name_data.hpp"
#include "src/buildtool/build_engine/base_maps/field_expression.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/expression_ptr.hpp"
#include "src/buildtool/common/repository_config.hpp"
#include "src/buildtool/multithreading/async_map_consumer.hpp"

namespace BuildMaps::Base {

[[nodiscard]] static inline auto GetOrDefault(nlohmann::json const& json,
                                              std::string const& key,
                                              nlohmann::json&& default_value)
    -> nlohmann::json {
    auto value = json.find(key);
    if (value != json.end()) {
        return value.value();
    }
    return std::move(default_value);
}

class FieldReader {
  public:
    using Ptr = std::shared_ptr<FieldReader>;

    class EntityAliases {
      public:
        [[nodiscard]] auto Obtain() && -> std::pair<std::vector<std::string>,
                                                    std::vector<EntityName>> {
            return std::make_pair(std::move(names_), std::move(ids_));
        }
        auto reserve(std::size_t size) -> void {
            names_.reserve(size);
            ids_.reserve(size);
        }
        template <class TName, class TId>
        auto emplace_back(TName&& name, TId&& id) -> void {
            names_.emplace_back(std::forward<TName>(name));
            ids_.emplace_back(std::forward<TId>(id));
        }

      private:
        std::vector<std::string> names_;
        std::vector<EntityName> ids_;
    };

    [[nodiscard]] static auto Create(nlohmann::json const& json,
                                     EntityName const& id,
                                     std::string const& entity_type,
                                     AsyncMapConsumerLoggerPtr const& logger)
        -> std::optional<FieldReader> {
        return Create(EntityDefinition{json}, id, entity_type, logger);
    }

    [[nodiscard]] static auto Create(EntityDefinition desc,
                                     EntityName const& id,
                                     std::string const& entity_type,
                                     AsyncMapConsumerLoggerPtr const& logger)
        -> std::optional<FieldReader> {
        if (not desc.IsObject()) {
            (*logger)(fmt::format("{} definition {} is not an object.",
                                  entity_type,
                                  id.GetNamedTarget().name),
                      true);
            return std::nullopt;
        }
        return FieldReader(std::move(desc), id, entity_type, logger);
    }

    [[nodiscard]] static auto CreatePtr(EntityDefinition desc,
                                        EntityName const& id,
                                        std::string const& entity_type,
                                        AsyncMapConsumerLoggerPtr const& logger)
        -> Ptr {
        if (not desc.IsObject()) {
            (*logger)(fmt::format("{} definition {} is not an object.",
                                  entity_type,
                                  id.GetNamedTarget().name),
                      true);
            return nullptr;
        }
        return std::make_shared<FieldReader>(
            std::move(desc), id, entity_type, logger);
    }

    [[nodiscard]] auto ReadExpression(std::string const& field_name) const
        -> ExpressionPtr {
        auto json = ReadJson(field_name);
        if (not json) {
            return ExpressionPtr{nullptr};
        }
        if (not *json) {
            LogMissingField(field_name);
            return ExpressionPtr{nullptr};
        }
        return ToExpression(**json);
    }

    [[nodiscard]] auto ReadOptionalExpression(
        std::string const& field_name,
        ExpressionPtr const& default_value) const -> ExpressionPtr {
        auto json = ReadJson(field_name);
        if (not json) {
            return ExpressionPtr{nullptr};
        }
        if (not *json) {
            return default_value;
        }
        return ToExpression(**json);
    }

    /// \brief Read field as expression for evaluation in a configuration.
    [[nodiscard]] auto ReadFieldExpression(std::string const& field_name) const
        -> FieldExpression {
        auto expr = desc_.ReadFieldExpression(field_name);
        if (not expr) {
            LogMissingField(field_name);
            return FieldExpression{};
        }
        if (not *expr) {
            LogConversionFailure();
        }
        return *std::move(expr);
    }

    /// \brief Read optional field as expression for evaluation in a
    /// configuration.
    [[nodiscard]] auto ReadOptionalFieldExpression(
        std::string const& field_name,
        ExpressionPtr const& default_value) const -> FieldExpression {
        auto expr = desc_.ReadFieldExpression(field_name);
        if (not expr) {
            return default_value;
        }
        if (not *expr) {
            LogConversionFailure();
        }
        return *std::move(expr);
    }

    /// \brief Read map field as expressions (one per entry) for evaluation in
    /// a configuration. A missing field is read as empty map.
    [[nodiscard]] auto ReadFieldExpressionMap(std::string const& field_name)
        const -> std::optional<std::map<std::string, FieldExpression>> {
        auto exprs = desc_.ReadFieldExpressions(
            field_name, [this](std::string const& msg) {
                (*logger_)(fmt::format("In {} {}:\n{}",
                                       entity_type_,
                                       id_.GetNamedTarget().name,
                                       msg),
                           true);
            });
        if (not exprs) {
            return std::nullopt;
        }
        return exprs->value_or(std::map<std::string, FieldExpression>{});
    }

    [[nodiscard]] auto ReadStringList(std::string const& field_name) const
        -> std::optional<std::vector<std::string>> {
        auto json = ReadJson(field_name);
        if (not json) {
            return std::nullopt;
        }
        auto const& list = json->value_or(nlohmann::json::array());
        if (not list.is_array()) {
            (*logger_)(fmt::format("Field {} in {} {} is not a list",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return std::nullopt;
        }

        auto vars = std::vector<std::string>{};
        vars.reserve(list.size());

        try {
            std::transform(
                list.begin(),
                list.end(),
                std::back_inserter(vars),
                [](auto const& j) { return j.template get<std::string>(); });
        } catch (...) {
            (*logger_)(fmt::format("List entry in {} of {} {} is not a string",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return std::nullopt;
        }

        return vars;
    }

    [[nodiscard]] auto ReadEntityAliasesObject(
        std::string const& field_name,
        gsl::not_null<const RepositoryConfig*> const& repo_config) const
        -> std::optional<EntityAliases> {
        auto json = ReadJson(field_name);
        if (not json) {
            return std::nullopt;
        }
        auto const& map = json->value_or(nlohmann::json::object());
        if (not map.is_object()) {
            (*logger_)(fmt::format("Field {} in {} {} is not an object",
                                   field_name,
                                   entity_type_,
                                   id_.GetNamedTarget().name),
                       true);
            return std::nullopt;
        }

        auto imports = EntityAliases{};
        imports.reserve(map.size());

        for (auto const& [key, val] : map.items()) {
            auto expr_id = ParseEntityNameFromJson(
                val,
                id_,
                repo_config,
                [this, &field_name, entry = val.dump()](
                    std::string const& parse_err) {
                    (*logger_)(fmt::format("Parsing entry {} in field {} of {} "
                                           "{} failed with:\n{}",
                                           entry,
                                           field_name,
                                           entity_type_,
                                           id_.GetNamedTarget().name,
                                           parse_err),
                               true);
                });
            if (not expr_id) {
                return std::nullopt;
            }
            imports.emplace_back(key, *expr_id);
        }
        return imports;
    }

    void ExpectFields(std::unordered_set<std::string> const& expected) {
        auto unexpected = nlohmann::json::array();
        for (auto const& key : desc_.FieldNames()) {
            if (not expected.contains(key)) {
                unexpected.push_back(key);
            }
        }
        if (not unexpected.empty()) {
            (*logger_)(fmt::format("{} {} has unexpected parameters {}",
                                   entity_type_,
                                   id_.ToString(),
                                   unexpected.dump()),
                       false);
        }
    }

    FieldReader(EntityDefinition desc,
                EntityName id,
                std::string entity_type,
                AsyncMapConsumerLoggerPtr logger) noexcept
        : desc_(std::move(desc)),
          id_{std::move(id)},
          entity_type_{std::move(entity_type)},
          logger_{std::move(logger)} {}

  private:
    EntityDefinition desc_;
    EntityName id_;
    std::string entity_type_;
    AsyncMapConsumerLoggerPtr logger_;

    [[nodiscard]] auto ReadJson(std::string const& field_name) const
        -> expected<std::optional<nlohmann::json>, std::monostate> {
        return desc_.ReadJson(field_name, [this](std::string const& msg) {
            (*logger_)(fmt::format("In {} {}:\n{}",
                                   entity_type_,
                                   id_.GetNamedTarget().name,
                                   msg),
                       true);
        });
    }

    [[nodiscard]] auto ToExpression(nlohmann::json const& json) const
        -> ExpressionPtr {
        auto expr = Expression::FromJson(json);
        if (not expr) {
            LogConversionFailure();
        }
        return expr;
    }

    void LogMissingField(std::string const& field_name) const {
        (*logger_)(fmt::format("Missing mandatory field {} in {} {}.",
                               field_name,
                               entity_type_,
                               id_.GetNamedTarget().name),
                   true);
    }

    void LogConversionFailure() const {
        (*logger_)(fmt::format("Failed to create expression from JSON:\n  {}",
                               desc_.ToJson().dump()),
                   true);
    }
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_FIELD_READER_HPP
