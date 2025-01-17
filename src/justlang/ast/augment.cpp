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

#include "justlang/ast/augment.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "justlang/ast/ast.hpp"

namespace {

auto const kUnevaluatedTargetFields =
    std::unordered_map<std::string, std::unordered_set<std::string>>{
        // default
        {"", {"type", "arguments_config"}},
        // export target
        {"export",
         {"type", "arguments_config", "fixed_config", "doc", "config_doc"}}};

[[nodiscard]] auto JustAugmentTarget(justlang::MapNode const* target)
    -> justlang::ASTNodePtr {
    using justlang::ASTNode;
    using justlang::ASTNodePtr;
    using justlang::VerbatimType;

    auto fields = target->GetFields();
    auto const& loc = target->GetLocation();

    auto target_type = std::string{};
    auto field_names = std::vector<std::string const*>{};
    field_names.reserve(fields.size());
    for (auto& field : fields) {
        auto const* field_name =
            ASTNode::Cast<justlang::StringNode const*>(field.first.get());
        if (field_name == nullptr) {
            throw justlang::ASTAugmentError{
                "Expected literal string as field name for literal map."};
        }
        if (field_name->GetValue() == "type") {
            if (auto const* str = ASTNode::Cast<justlang::StringNode const*>(
                    field.second.get())) {
                target_type = str->GetValue();
            }
            else if (ASTNode::Cast<justlang::RefNode const*>(
                         field.second.get()) == nullptr) {
                throw justlang::ASTAugmentError{
                    "Expected literal string or reference for target type."};
            }
            // else: not a builtin rule, thats fine
        }
        field_names.emplace_back(&field_name->GetValue());
    }
    auto const& uneval_fields = kUnevaluatedTargetFields.contains(target_type)
                                    ? kUnevaluatedTargetFields.at(target_type)
                                    : kUnevaluatedTargetFields.at("");
    std::size_t pos{};
    for (auto& field : fields) {
        auto const& name = *field_names[pos++];
        if (uneval_fields.contains(name)) {
            field.second = std::make_shared<justlang::VerbatimNode>(
                field.second->GetLocation(), field.second, VerbatimType::Full);
        }
    }
    return std::make_shared<justlang::VerbatimNode>(
        loc,
        std::make_shared<justlang::MapNode>(loc, std::move(fields)),
        VerbatimType::Flat);
}

}  // namespace

auto justlang::JustAugmentAST(justlang::ASTNodePtr const& expr)
    -> justlang::ASTNodePtr {
    if (auto const* targets_obj = ASTNode::Cast<MapNode const*>(expr.get())) {
        auto targets = targets_obj->GetFields();
        auto const& loc = targets_obj->GetLocation();

        for (auto& target : targets) {
            auto const* target_name =
                ASTNode::Cast<StringNode const*>(target.first.get());
            auto const* target_obj =
                ASTNode::Cast<MapNode const*>(target.second.get());
            if (target_name == nullptr) {
                throw ASTAugmentError{
                    "Expected literal string as field name for literal map."};
            }
            if (target_obj == nullptr) {
                throw ASTAugmentError{"AST node is not a literal map."};
            }
            target.second = JustAugmentTarget(target_obj);
        }

        return std::make_shared<VerbatimNode>(
            loc,
            std::make_shared<MapNode>(loc, std::move(targets)),
            VerbatimType::Flat);
    }
    throw ASTAugmentError{"AST node is not a literal map."};
}
