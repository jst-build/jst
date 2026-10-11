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

#ifndef INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITIES_FILE_HPP
#define INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITIES_FILE_HPP

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_definition.hpp"
#include "src/buildtool/build_engine/base_maps/jstlang_entities_file.hpp"

namespace BuildMaps::Base {

/// \brief Content of a file of entities (e.g., TARGETS or RULES): either a JSON
/// object or a jstlang file for runtime evaluation.
class EntitiesFile final {
  public:
    using Logger = EntityDefinition::Logger;

    explicit EntitiesFile(nlohmann::json json) noexcept
        : data_{std::move(json)} {}

    explicit EntitiesFile(JstlangEntitiesFile::Ptr file) noexcept
        : data_{std::move(file)} {}

    /// \brief Whether the file content is a map of entities.
    [[nodiscard]] auto IsObject() const noexcept -> bool {
        auto const* json = std::get_if<nlohmann::json>(&data_);
        return json == nullptr or json->is_object();
    }

    [[nodiscard]] auto Contains(std::string const& name) const -> bool {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            return json->contains(name);
        }
        return std::get<JstlangEntitiesFile::Ptr>(data_)->Contains(name);
    }

    /// \brief The names of the entities in lexicographic order.
    [[nodiscard]] auto Names() const -> std::vector<std::string> {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            auto names = std::vector<std::string>{};
            names.reserve(json->size());
            for (auto const& [key, _] : json->items()) {
                names.emplace_back(key);
            }
            return names;
        }
        return std::get<JstlangEntitiesFile::Ptr>(data_)->Names();
    }

    /// \brief Obtain the definition of an entity.
    /// \returns nullopt if name is not defined or on error (reported via
    /// logger).
    [[nodiscard]] auto GetEntity(std::string const& name, Logger const& logger)
        const -> std::optional<EntityDefinition> {
        if (auto const* json = std::get_if<nlohmann::json>(&data_)) {
            auto it = json->find(name);
            if (it == json->end()) {
                return std::nullopt;
            }
            return EntityDefinition{*it};
        }
        auto const& file = std::get<JstlangEntitiesFile::Ptr>(data_);
        if (not file->Contains(name)) {
            return std::nullopt;
        }
        if (auto fields = file->GetFields(name, logger)) {
            return EntityDefinition{*std::move(fields)};
        }
        return std::nullopt;
    }

  private:
    std::variant<nlohmann::json, JstlangEntitiesFile::Ptr> data_;
};

}  // namespace BuildMaps::Base

#endif  // INCLUDED_SRC_BUILDTOOL_BUILD_ENGINE_BASE_MAPS_ENTITIES_FILE_HPP
