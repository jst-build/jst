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

#include "justlang/ref.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/json_string.hpp"

namespace {

auto const kSepToken = std::string{":"};
auto const kAbsToken = std::string{"//"};
auto const kRelToken = std::string{"./"};

class RefDecodeError : public std::runtime_error {
  public:
    explicit RefDecodeError(std::string const& ref_str,
                            std::string const& error)
        : runtime_error{"Decoding reference '" + ref_str +
                        "' failed with:\n  " + error} {}
};

class RefEncodeError : public std::runtime_error {
  public:
    explicit RefEncodeError(std::string const& segment,
                            std::string const& error)
        : runtime_error{"Encoding reference segment '" + segment +
                        "' failed with:\n  " + error} {}
};

[[nodiscard]] auto StartsWith(std::size_t pos,
                              std::string const& str,
                              std::string const& cmp) -> bool {
    if (pos + cmp.length() > str.length()) {
        return false;
    }
    auto start = str.cbegin() + static_cast<int>(pos);
    auto view = std::string_view{start, start + static_cast<int>(cmp.length())};
    return view == cmp;
}

[[nodiscard]] auto FindNext(std::size_t pos,
                            std::string const& str,
                            std::string const& cmp)
    -> std::optional<std::size_t> {
    auto fpos = str.find(cmp, pos);
    if (fpos == std::string::npos) {
        return std::nullopt;
    }
    return fpos;
}

struct ParseResult {
    std::string name;
    std::size_t next_pos;
};

[[nodiscard]] auto ParseSegment(std::size_t pos,
                                std::string const& str,
                                std::string const* end = nullptr)
    -> ParseResult {
    if (str[pos] == '"') {
        ++pos;
        for (auto i = pos; i < str.length(); ++i) {
            if (str[i] == '"' and (i == pos || str[i - 1] != '\\')) {
                if (auto name = justlang::UnescapeJsonString(
                        str.substr(pos, i - pos))) {
                    return ParseResult{.name = std::move(*name),
                                       .next_pos = i + 1};
                }
                throw RefDecodeError{str, "Invalid JSON string."};
            }
        }
        throw RefDecodeError{str, "Unterminated quote."};
    }
    if (end != nullptr) {
        if (auto next = FindNext(pos, str, *end)) {
            return ParseResult{.name = str.substr(pos, *next - pos),
                               .next_pos = *next};
        }
    }
    return ParseResult{.name = str.substr(pos), .next_pos = str.length()};
}

[[nodiscard]] auto NormPath(std::filesystem::path const& path) noexcept
    -> std::string {
    auto norm = path.lexically_normal();
    if (not norm.has_filename()) {
        norm = norm.parent_path();
    }
    if (norm.empty()) {
        return std::filesystem::path{"."}.string();
    }
    return norm.string();
}

[[nodiscard]] auto NormModule(std::string const& module) noexcept
    -> std::string {
    auto norm = NormPath(module);
    return (norm == ".") ? std::string{""} : norm;
}

[[nodiscard]] auto ParseTarget(std::size_t pos,
                               std::string const& str) -> std::string {
    if (pos < str.length()) {
        return ParseSegment(pos, str).name;
    }
    throw RefDecodeError{str, "Missing target name after ':'."};
}

[[nodiscard]] auto ParseAbs(std::size_t pos, std::string const& str)
    -> std::tuple<std::string, std::string> {
    auto segment = ParseSegment(pos, str, &kSepToken);
    auto module = NormModule(segment.name);
    pos = segment.next_pos;
    if (StartsWith(pos, str, kSepToken)) {
        pos += kSepToken.length();
        return {std::move(module), ParseTarget(pos, str)};
    }
    if (segment.name.empty()) {
        throw RefDecodeError{str,
                             "Empty module without target is not allowed."};
    }
    if (pos < str.length()) {
        throw RefDecodeError{str, "Missing ':' after module name."};
    }
    return {module, std::filesystem::path(module).filename().string()};
}

[[nodiscard]] auto ParseExt(std::size_t pos,
                            std::string const& str,
                            bool file_ref)
    -> std::tuple<std::string, std::string, std::string> {
    auto segment = ParseSegment(pos, str, &kAbsToken);
    if (segment.next_pos < str.length()) {
        pos = segment.next_pos + kAbsToken.length();
        if (pos < str.length()) {
            auto repo = std::move(segment.name);
            if (file_ref) {
                return {std::move(repo), str.substr(pos), ""};
            }
            auto [module, target] = ParseAbs(pos, str);
            return {std::move(repo), std::move(module), std::move(target)};
        }
        throw RefDecodeError{str, "Missing module name after '//'."};
    }
    throw RefDecodeError{str, "Missing '//' after repository name."};
}

}  // namespace

auto justlang::DecodeRefString(std::string const& ref_str,
                               bool file_ref) -> justlang::RefData {
    RefData data{};

    if (StartsWith(0, ref_str, kSepToken)) {
        // ':<target>'
        data.type = RefType::Local;
        data.target = ParseTarget(kSepToken.length(), ref_str);
    }
    else if (StartsWith(0, ref_str, kAbsToken)) {
        // '//<module>:<target>'
        data.type = RefType::Abs;
        std::tie(data.module, data.target) =
            ParseAbs(kAbsToken.length(), ref_str);
    }
    else if (StartsWith(0, ref_str, kRelToken)) {
        // './<submodule>:<target>'
        data.type = RefType::Rel;
        std::tie(data.module, data.target) =
            ParseAbs(kRelToken.length(), ref_str);
    }
    else {
        // '<repo>//<submodule>:<target>'
        data.type = RefType::Ext;
        std::tie(data.repo, data.module, data.target) =
            ParseExt(0, ref_str, file_ref);
    }

    return data;
}

auto justlang::QuoteRefSegment(std::string const& segment,
                               RefSegment seg_type,
                               bool allow_empty) -> std::string {
    bool needs_quoting = segment.empty() and not allow_empty;
    switch (seg_type) {
        case RefSegment::Repo:
            // may not start with ':', './', or contain '//'
            needs_quoting |= StartsWith(0, segment, kSepToken) or
                             StartsWith(0, segment, kRelToken) or
                             FindNext(0, segment, kAbsToken).has_value();
            break;
        case RefSegment::Module:
            // may not contain ':'
            needs_quoting |= FindNext(0, segment, kSepToken).has_value();
            break;
        default:
            break;
    }
    try {
        auto json_str = nlohmann::json(segment).dump();
        if (needs_quoting or json_str != "\"" + segment + "\"") {
            return json_str;
        }
    } catch (std::exception const& e) {
        throw RefEncodeError{
            segment, std::string{"Encoding JSON failed with: "} + e.what()};
    }
    return segment;
}

auto justlang::EncodeRefData(justlang::RefData const& data) -> std::string {
    auto str = std::string{};
    switch (data.type) {
        case RefType::Ext:
            str += QuoteRefSegment(data.repo, RefSegment::Repo);
            [[fallthrough]];
        case RefType::Abs:
        case RefType::Rel:
            str += data.type == RefType::Rel ? kRelToken : kAbsToken;
            str += QuoteRefSegment(
                data.module, RefSegment::Module, /*allow_empty=*/true);
            [[fallthrough]];
        case RefType::Local:
            str += kSepToken + QuoteRefSegment(data.target, RefSegment::Target);
    }
    return str;
}
