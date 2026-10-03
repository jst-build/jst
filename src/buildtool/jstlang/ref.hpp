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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_REF_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_REF_HPP

#include <cstdint>
#include <optional>
#include <string>

namespace jstlang {

enum class RefType : std::uint8_t { Ext, Abs, Rel, Local };
enum class RefSegment : std::uint8_t { Repo, Module, Target };

/// \brief Where a reference is given, which decides what it may encode.
enum class RefContext : std::uint8_t {
    Target,  ///< A target-reference in a target file.
    File,    ///< A file-reference, '[<repo>]//<file_path>'.
    CLI,     ///< A target-reference on the CLI; target segments may be omitted.
};

struct RefData {
    RefType type;
    std::optional<std::string> repo;    ///< Repo name, if provided.
    std::string module;                 ///< Module name; may be empty.
    std::optional<std::string> target;  ///< Target name, if not omitted.
};

/// \brief Decode ref-string (target, file, or CLI-reference) to RefData.
/// \param ctx  Where the ref-string is given.
/// Possible target-reference encodings are:
///   - Local:      ':<target>'
///   - Rel:        './<module>[:<target>]'
///   - Abs|Ext:    '[<repo>]//<module>[:<target>]'
/// Possible file-reference encoding is:
///   - Abs|Ext:    '[<repo>]//<file_path>'   (<file_path> in RefData::module)
/// Possible target-reference encoding on the CLI is:
///   - Local:      ':[<target>]'
///   - Rel:        './<module>[:[<target>]]'
///   - Abs|Ext:    '[<repo>]//<module>[:[<target>]]'
[[nodiscard]] auto DecodeRefString(std::string const& ref_str,
                                   RefContext ctx = RefContext::Target)
    -> RefData;

/// \brief Quote single reference segment (repo, module, target)
/// \param segment      The segment string to quote.
/// \param seg_type     The segment type (repo, module, target).
/// \param allow_empty  Do not quote empty segments (useful for module).
[[nodiscard]] auto QuoteRefSegment(std::string const& segment,
                                   RefSegment seg_type,
                                   bool allow_empty = false) -> std::string;

[[nodiscard]] auto EncodeRefData(RefData const& data) -> std::string;

}  // namespace jstlang

#endif
