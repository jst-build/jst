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

#ifndef JUSTLANG_JSON_STRING_HPP
#define JUSTLANG_JSON_STRING_HPP

#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

namespace justlang {

[[nodiscard]] inline static auto UnescapeJsonString(
    std::string const& jstr) noexcept -> std::optional<std::string> {
    try {
        return nlohmann::json::parse('"' + jstr + '"').get<std::string>();
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace justlang

#endif
