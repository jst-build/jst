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

#ifndef JUSTLANG_AST_INLINE_VISITOR_HPP
#define JUSTLANG_AST_INLINE_VISITOR_HPP

#include <concepts>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "justlang/ast/ast.hpp"

namespace justlang {

class ScopeDefs {
  public:
    ScopeDefs() = default;
    explicit ScopeDefs(ScopeDefs const* upper) : upper_{upper} {}
    ScopeDefs(ScopeDefs const&) = delete;
    ScopeDefs(ScopeDefs&&) = delete;
    ~ScopeDefs() = default;
    auto operator=(ScopeDefs const&) = delete;
    auto operator=(ScopeDefs&&) = delete;

    void AddDef(std::string name, ASTNodePtr def) {
        defs_.insert_or_assign(std::move(name), std::move(def));
    }

    [[nodiscard]] auto GetDef(std::string const& name) const
        -> ASTNodePtr const* {
        auto def_it = defs_.find(name);
        if (def_it != defs_.end()) {
            return &def_it->second;
        }
        return upper_ != nullptr ? upper_->GetDef(name) : nullptr;
    }

  private:
    std::unordered_map<std::string, ASTNodePtr> defs_;
    ScopeDefs const* upper_{nullptr};
};

template <class T, class... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

template <class T>
concept IsLeafNode = IsAnyOf<T,
                             justlang::NullNode,
                             justlang::BoolNode,
                             justlang::StringNode,
                             justlang::NumberNode,
                             justlang::VarNode,
                             justlang::RefNode,
                             justlang::BuiltinNode>;

class ASTInlineVisitor final {
  private:
    template <class T>
    [[nodiscard]] static auto ShallowCopy(T const* node) -> std::shared_ptr<T> {
        return std::make_shared<T>(*node);
    }

    [[nodiscard]] auto InlineFuncBody(CallNode const* caller,
                                      FuncNode const* callee) const
        -> ASTNodePtr;

    [[nodiscard]] auto InlineBuiltinCall(CallNode const* caller,
                                         BuiltinNode const* builtin) const
        -> ASTNodePtr;

  public:
    explicit ASTInlineVisitor(ScopeDefs const* scope,
                              bool partial_inline) noexcept
        : scope_{scope}, partial_inline_{partial_inline} {}

    [[nodiscard]] auto InlineFunctions(ASTNodePtr const& node) const
        -> ASTNodePtr;

    // default operator for leaf nodes (no inlining needed)
    template <class T>
    [[nodiscard]] auto operator()(T const* node) const -> ASTNodePtr {
        // use static_assert to check concept with meaningful error message
        static_assert(IsLeafNode<T>,
                      "non-leaf nodes require a specialized operator");
        return ShallowCopy(node);
    }

    [[nodiscard]] auto operator()(LetNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(FuncNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(CallNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ForeignNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(VerbatimNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ListNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(MapNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(IfNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ForEachNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(ZipWithNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(FoldLeftNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(UnaryOperationNode const* node) const
        -> ASTNodePtr;
    [[nodiscard]] auto operator()(LookupNode const* node) const -> ASTNodePtr;
    [[nodiscard]] auto operator()(BinaryOperationNode const* node) const
        -> ASTNodePtr;

  private:
    ScopeDefs const* scope_;
    bool partial_inline_;
};

}  // namespace justlang

#endif  // JUSTLANG_AST_INLINE_VISITOR_HPP
