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

#ifndef JUSTLANG_REF_HPP
#define JUSTLANG_REF_HPP

#include <cstdint>
#include <string>

namespace justlang {

enum class RefType : std::uint8_t { Ext, Abs, Rel, Local };
enum class RefSegment : std::uint8_t { Repo, Module, Target };

struct RefData {
    RefType type;
    std::string repo;
    std::string module;
    std::string target;
};

/// \brief Decode ref-string (target or file-reference) to RefData.
/// \param file_ref     Ref-string is a file-reference.
/// Possible target-reference encodings are:
///   - Local:  ':<target>'
///   - Rel:    './<module>[:<target>]'
///   - Abs:    '//<module>[:<target>]'
///   - Ext:    '<repo>//<module>[:<target>]'
/// Possible file-reference encoding is:
///   - Ext:    '<repo>//<file_path>'   (<file_path> in RefData::module)
[[nodiscard]] auto DecodeRefString(std::string const& ref_str,
                                   bool file_ref = false) -> RefData;

[[nodiscard]] auto QuoteRefSegment(std::string const& segment,
                                   RefSegment seg_type,
                                   bool allow_empty = false) -> std::string;

[[nodiscard]] auto EncodeRefData(RefData const& data) -> std::string;

}  // namespace justlang

#endif
