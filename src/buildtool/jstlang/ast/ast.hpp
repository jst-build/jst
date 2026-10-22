// Copyright 2025 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_AST_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_AST_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "nlohmann/json.hpp"
#include "src/buildtool/jstlang/ast/node_variant.hpp"
#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/ref.hpp"
#include "src/utils/cpp/gsl.hpp"

namespace jstlang {

enum class ValueType : std::uint8_t {
    Null,
    Bool,
    Number,
    String,
    List,
    Map,
    Ref,
    Func,
    Artifact,
    Result,
    AnonTarget,
    EntityName,
    Any
};

[[nodiscard]] static inline auto TypeToString(jstlang::ValueType type) noexcept
    -> std::string {
    switch (type) {
        case jstlang::ValueType::Null:
            return "Null";
        case jstlang::ValueType::Bool:
            return "Bool";
        case jstlang::ValueType::Number:
            return "Number";
        case jstlang::ValueType::String:
            return "String";
        case jstlang::ValueType::List:
            return "List";
        case jstlang::ValueType::Map:
            return "Map";
        case jstlang::ValueType::Ref:
            return "Ref";
        case jstlang::ValueType::Func:
            return "Func";
        case jstlang::ValueType::Artifact:
            return "Artifact";
        case jstlang::ValueType::Result:
            return "Result";
        case jstlang::ValueType::AnonTarget:
            return "AnonTarget";
        case jstlang::ValueType::EntityName:
            return "EntityName";
        case jstlang::ValueType::Any:
            return "Any";
    }
    return "";  // unreachable
}

class ASTNode;
using ASTNodePtr = std::shared_ptr<ASTNode>;

/// \brief Lexical scope of lazily evaluated bindings, see scope.hpp.
class Scope;
using ScopePtr = std::shared_ptr<Scope const>;

/// \brief Kind of an AST node, one per node class (same order as in
/// ASTNodeVariant).
enum class NodeKind : std::uint8_t {
    Let,
    Func,
    Builtin,
    BuiltinCall,
    Call,
    Foreign,
    List,
    Map,
    Bool,
    String,
    Number,
    Null,
    Var,
    If,
    ForEach,
    ZipWith,
    FoldLeft,
    UnaryOperation,
    Lookup,
    BinaryOperation,
    Ref,
    Closure,
    Artifact,
    Result,
    AnonTarget,
    EntityName,
};

/// \brief Position in a source file (compact, the source file is owned by the
/// ASTContext the node was created with). Not to be confused with
/// FileLocation, the location of a file among the repositories.
struct Location {
    SourceFile const* source{};  // nullptr if unknown
    std::uint32_t line{};
    std::uint32_t column{};
    std::uint32_t column_end{};

    /// \brief Name of the source file, empty if unknown.
    [[nodiscard]] auto File() const -> std::string {
        return source != nullptr ? source->location.ToString() : std::string{};
    }
};

class ASTNode {
  public:
    explicit ASTNode(NodeKind kind,
                     Location loc,
                     ValueType value_type,
                     std::optional<bool> truth_value = std::nullopt) noexcept
        : loc_{loc},
          kind_{kind},
          value_type_{value_type},
          truth_value_{truth_value} {}
    ASTNode(ASTNode&&) = delete;
    ASTNode(ASTNode const&) = default;
    auto operator=(ASTNode&&) -> ASTNode& = default;
    auto operator=(ASTNode const&) -> ASTNode& = delete;

    [[nodiscard]] auto Kind() const noexcept -> NodeKind { return kind_; }

    [[nodiscard]] auto EvalType() const noexcept -> ValueType {
        return value_type_;
    }

    [[nodiscard]] auto GetLocation() const noexcept -> Location const& {
        return loc_;
    }

    [[nodiscard]] auto TruthValue() const noexcept
        -> std::optional<bool> const& {
        return truth_value_;
    }

    /// \brief Whether node is a literal value (null, bool, number, string,
    /// reference, a value of the build engine, or a list or map of those),
    /// i.e., contains no residual code and no functions. Computed on
    /// construction.
    [[nodiscard]] auto IsLiteral() const noexcept -> bool {
        return is_literal_;
    }

    /// \brief Whether node is a function (by syntax or as closure).
    [[nodiscard]] auto IsFunction() const noexcept -> bool {
        return kind_ == NodeKind::Func or kind_ == NodeKind::Closure;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant;

    template <std::convertible_to<ASTNodeVariant> TNode,
              typename TUnderlying = std::remove_pointer_t<TNode>>
        requires(std::is_const_v<TUnderlying>)
    [[nodiscard]] static auto Cast(ASTNode const* node) noexcept -> TNode {
        if (node == nullptr or node->kind_ != TUnderlying::kKind) {
            return nullptr;
        }
        // the kind tag ensures the node has the derived type
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return static_cast<TNode>(node);
    }

    /// \brief Cast shared pointer to derived node, nullptr on kind mismatch.
    template <typename TDerived>
    [[nodiscard]] static auto CastPtr(ASTNodePtr const& node) noexcept
        -> std::shared_ptr<TDerived> {
        if (node == nullptr or node->kind_ != TDerived::kKind) {
            return nullptr;
        }
        return std::static_pointer_cast<TDerived>(node);
    }

  protected:
    /// \brief Set by literals on construction (by lists and maps after
    /// construction of their children).
    void SetLiteral(bool is_literal) noexcept { is_literal_ = is_literal; }

    ~ASTNode() = default;

  private:
    Location loc_;
    NodeKind kind_;
    ValueType value_type_;
    std::optional<bool> truth_value_;
    bool is_literal_{false};
};

class LetNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Let;

    explicit LetNode(Location loc,
                     std::string name,
                     ASTNodePtr value,
                     ASTNodePtr next) noexcept
        : ASTNode{kKind, loc, next->EvalType()},
          name_{std::move(name)},
          value_{std::move(value)},
          next_{std::move(next)} {}

    [[nodiscard]] auto GetName() const noexcept -> std::string const& {
        return name_;
    }

    [[nodiscard]] auto GetValue() const noexcept -> ASTNodePtr const& {
        return value_;
    }

    [[nodiscard]] auto GetNext() const noexcept -> ASTNodePtr const& {
        return next_;
    }

  private:
    std::string name_;
    ASTNodePtr value_;
    ASTNodePtr next_;
};

// function definition
class FuncNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Func;

    using param_t = std::pair<std::string, ASTNodePtr>;
    using params_t = std::vector<param_t>;

    explicit FuncNode(Location loc, params_t params, ASTNodePtr body) noexcept
        : ASTNode{kKind, loc, ValueType::Func},
          params_{std::move(params)},
          body_{std::move(body)} {}

    [[nodiscard]] auto GetParams() const noexcept -> params_t const& {
        return params_;
    }

    [[nodiscard]] auto GetBody() const noexcept -> ASTNodePtr const& {
        return body_;
    }

  private:
    params_t params_;
    ASTNodePtr body_;
};

class BuiltinNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Builtin;

    explicit BuiltinNode(std::string name) noexcept
        : ASTNode{kKind, {}, ValueType::Func}, name_{std::move(name)} {}

    [[nodiscard]] auto GetName() const noexcept -> std::string const& {
        return name_;
    }

  private:
    std::string name_;
};

/// \brief Call of a built-in function that cannot be evaluated (yet), e.g.,
/// because some of its arguments are not known. The arguments are stored by
/// the built-in's parameter position; unspecified arguments are nullptr.
class BuiltinCallNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::BuiltinCall;

    using args_t = std::vector<ASTNodePtr>;

    explicit BuiltinCallNode(Location loc,
                             std::string name,
                             args_t args,
                             ValueType eval_type) noexcept
        : ASTNode{kKind, loc, eval_type},
          name_{std::move(name)},
          args_{std::move(args)} {}

    [[nodiscard]] auto GetName() const noexcept -> std::string const& {
        return name_;
    }

    [[nodiscard]] auto GetArgs() const noexcept -> args_t const& {
        return args_;
    }

  private:
    std::string name_;
    args_t args_;
};

// call to function (or read of variable, if no params)
class CallNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Call;

    using param_t = std::pair<std::optional<std::string>, ASTNodePtr>;
    using params_t = std::vector<param_t>;

    using identifier_t = std::string;
    using expr_t = ASTNodePtr;
    using builtin_t = std::shared_ptr<BuiltinNode>;
    using target_t = std::variant<identifier_t, expr_t, builtin_t>;

    explicit CallNode(Location loc,
                      target_t target,
                      std::optional<params_t> params = std::nullopt) noexcept
        : ASTNode{kKind, loc, ValueType::Any},
          target_{std::move(target)},
          params_{std::move(params)} {}

    [[nodiscard]] auto GetTarget() const noexcept -> target_t const& {
        return target_;
    }

    [[nodiscard]] auto GetParams() const noexcept
        -> std::optional<params_t> const& {
        return params_;
    }

    [[nodiscard]] auto IsVarRead() const noexcept -> bool {
        return not params_.has_value();
    }

  private:
    target_t target_;
    std::optional<params_t> params_;
};

// foreign self-contained AST (without access to upper scope)
/// \brief Imported file: either resolved (the AST of the file) or an
/// unresolved reference (parsing with deferred imports).
class ForeignNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Foreign;

    explicit ForeignNode(Location loc, ASTNodePtr expr) noexcept
        : ASTNode{kKind, loc, expr->EvalType()}, expr_{std::move(expr)} {}

    explicit ForeignNode(Location loc,
                         std::shared_ptr<FileLocation const> import) noexcept
        : ASTNode{kKind, loc, ValueType::Any}, import_{std::move(import)} {}

    /// \brief AST of the imported file, nullptr if unresolved.
    [[nodiscard]] auto GetExpr() const noexcept -> ASTNodePtr const& {
        return expr_;
    }

    /// \brief Location of the imported file, nullptr if resolved.
    [[nodiscard]] auto GetImport() const noexcept
        -> std::shared_ptr<FileLocation const> const& {
        return import_;
    }

  private:
    ASTNodePtr expr_;
    std::shared_ptr<FileLocation const> import_;
};

class ListNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::List;

    using items_t = std::vector<ASTNodePtr>;

    explicit ListNode(Location loc, items_t items) noexcept
        : ASTNode{kKind, loc, ValueType::List, not items.empty()},
          items_{std::move(items)} {
        SetLiteral(
            std::all_of(items_.begin(), items_.end(), [](auto const& item) {
                return item->IsLiteral();
            }));
    }
    ListNode(ListNode&&) = delete;
    ListNode(ListNode const&) = default;
    auto operator=(ListNode&&) -> ListNode& = delete;
    auto operator=(ListNode const&) -> ListNode& = delete;
    ~ListNode() = default;

    [[nodiscard]] auto GetItems() const noexcept -> items_t const& {
        return items_;
    }

  private:
    items_t items_{};
};

/// \brief Map with fields in order of definition, the latest field wins.
/// Field names that are string literals are unique (see MapNode::Unique).
class MapNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Map;

    using entry_t = std::pair<ASTNodePtr, ASTNodePtr>;
    using fields_t = std::vector<entry_t>;

    /// \brief Whether map is constructed with unique field names (if string).
    enum class Unique : std::uint8_t {
        Yes,    ///< Unique names, guaranteed by the creator (not checked).
        Maybe,  ///< Remove duplicates on construction, the latest field wins.
    };

    explicit MapNode(Location loc, fields_t fields, Unique unique = Unique::Yes)
        : ASTNode{kKind, loc, ValueType::Map, not fields.empty()},
          fields_{unique == Unique::Maybe
                      ? RemoveDuplicateFields(std::move(fields))
                      : std::move(fields)} {
        // make sure in debug mode that no duplicates slipped in
        EnsuresAudit(RemoveDuplicateFields(fields_).size() == fields_.size());
        SetLiteral(
            std::all_of(fields_.begin(), fields_.end(), [](auto const& field) {
                return Cast<class StringNode const*>(field.first.get()) !=
                           nullptr and
                       field.second->IsLiteral();
            }));
    }
    MapNode(MapNode&&) = delete;
    MapNode(MapNode const&) = default;
    auto operator=(MapNode&&) -> MapNode& = delete;
    auto operator=(MapNode const&) -> MapNode& = delete;
    ~MapNode() = default;

    [[nodiscard]] auto GetFields() const noexcept -> fields_t const& {
        return fields_;
    }

  private:
    fields_t fields_{};

    [[nodiscard]] static auto RemoveDuplicateFields(fields_t fields)
        -> fields_t;
};

class BoolNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Bool;

    explicit BoolNode(Location loc, bool value) noexcept
        : ASTNode{kKind, loc, ValueType::Bool, value}, value_{value} {
        SetLiteral(true);
    }
    BoolNode(BoolNode&&) = delete;
    BoolNode(BoolNode const&) = default;
    auto operator=(BoolNode&&) -> BoolNode& = delete;
    auto operator=(BoolNode const&) -> BoolNode& = delete;
    ~BoolNode() = default;

    [[nodiscard]] auto GetValue() const noexcept -> bool { return value_; }

  private:
    bool value_{};
};

class StringNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::String;

    explicit StringNode(Location loc, std::string value) noexcept
        : ASTNode{kKind, loc, ValueType::String, not value.empty()},
          value_{std::move(value)} {
        SetLiteral(true);
    }
    StringNode(StringNode&&) = delete;
    StringNode(StringNode const&) = default;
    auto operator=(StringNode&&) -> StringNode& = delete;
    auto operator=(StringNode const&) -> StringNode& = delete;
    ~StringNode() = default;

    [[nodiscard]] auto GetValue() const noexcept -> std::string const& {
        return value_;
    }

  private:
    std::string value_{};
};

class NumberNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Number;

    explicit NumberNode(Location loc, double value) noexcept
        : ASTNode{kKind, loc, ValueType::Number, value != 0.0}, value_{value} {
        SetLiteral(true);
    }

    NumberNode(NumberNode&&) = delete;
    NumberNode(NumberNode const&) = default;
    auto operator=(NumberNode&&) = delete;
    auto operator=(NumberNode const&) = delete;
    ~NumberNode() = default;

    [[nodiscard]] auto GetValue() const noexcept -> double { return value_; }

  private:
    double value_ = 0.;
};

class NullNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Null;

    explicit NullNode(Location loc) noexcept
        : ASTNode{kKind,
                  loc,
                  ValueType::Null,
                  /*truth_value=*/false} {
        SetLiteral(true);
    }
    NullNode(NullNode&&) = delete;
    NullNode(NullNode const&) = default;
    auto operator=(NullNode&&) -> NullNode& = delete;
    auto operator=(NullNode const&) -> NullNode& = delete;
    ~NullNode() = default;
};

class VarNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Var;

    explicit VarNode(Location loc,
                     std::string name,
                     ASTNodePtr default_expr = nullptr) noexcept
        : ASTNode{kKind, loc, ValueType::Any},
          name_{std::move(name)},
          default_{std::move(default_expr)} {}
    VarNode(VarNode&&) = delete;
    VarNode(VarNode const&) = default;
    auto operator=(VarNode&&) -> VarNode& = delete;
    auto operator=(VarNode const&) -> VarNode& = delete;
    ~VarNode() = default;

    [[nodiscard]] auto GetName() const noexcept -> std::string const& {
        return name_;
    }
    [[nodiscard]] auto GetDefault() const noexcept -> ASTNodePtr const& {
        return default_;
    }

  private:
    std::string name_{};
    ASTNodePtr default_{};
};

class IfNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::If;

    explicit IfNode(Location loc,
                    ASTNodePtr cond,
                    ASTNodePtr then_expr = nullptr,
                    ASTNodePtr else_expr = nullptr) noexcept
        : ASTNode{kKind,
                  loc,
                  GetEvalType(then_expr, else_expr),
                  GetTruthValue(then_expr, else_expr)},
          cond_{std::move(cond)},
          then_{std::move(then_expr)},
          else_{std::move(else_expr)} {}
    IfNode(IfNode&&) = delete;
    IfNode(IfNode const&) = default;
    auto operator=(IfNode&&) -> IfNode& = delete;
    auto operator=(IfNode const&) -> IfNode& = delete;
    ~IfNode() = default;

    [[nodiscard]] auto GetCondition() const noexcept -> ASTNodePtr const& {
        return cond_;
    }
    [[nodiscard]] auto GetThenBranch() const noexcept -> ASTNodePtr const& {
        return then_;
    }
    [[nodiscard]] auto GetElseBranch() const noexcept -> ASTNodePtr const& {
        return else_;
    }

  private:
    ASTNodePtr cond_{};
    ASTNodePtr then_{};
    ASTNodePtr else_{};

    [[nodiscard]] static auto GetEvalType(ASTNodePtr const& then_expr,
                                          ASTNodePtr const& else_expr) noexcept
        -> ValueType {
        auto const type1 =
            then_expr == nullptr ? ValueType::List : then_expr->EvalType();
        auto const type2 =
            else_expr == nullptr ? ValueType::List : else_expr->EvalType();
        if (type1 == type2) {
            return type1;
        }
        return ValueType::Any;
    }

    [[nodiscard]] static auto GetTruthValue(
        ASTNodePtr const& then_expr,
        ASTNodePtr const& else_expr) noexcept -> std::optional<bool> {
        auto const truth1 =
            then_expr == nullptr ? std::nullopt : then_expr->TruthValue();
        auto const truth2 =
            else_expr == nullptr ? std::nullopt : else_expr->TruthValue();
        if (truth1 and truth2 and *truth1 == *truth2) {
            return truth1;
        }
        return std::nullopt;
    }
};

class ForEachNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::ForEach;

    explicit ForEachNode(Location loc,
                         std::string var,
                         ASTNodePtr range,
                         ASTNodePtr body) noexcept
        : ASTNode{kKind, loc, ValueType::List},
          var_{std::move(var)},
          range_{std::move(range)},
          body_{std::move(body)} {}
    ForEachNode(ForEachNode&&) = delete;
    ForEachNode(ForEachNode const&) = default;
    auto operator=(ForEachNode&&) -> ForEachNode& = delete;
    auto operator=(ForEachNode const&) -> ForEachNode& = delete;
    ~ForEachNode() = default;

    [[nodiscard]] auto GetVariable() const noexcept -> std::string const& {
        return var_;
    }
    [[nodiscard]] auto GetRange() const noexcept -> ASTNodePtr const& {
        return range_;
    }
    [[nodiscard]] auto GetBody() const noexcept -> ASTNodePtr const& {
        return body_;
    }

  private:
    std::string var_{};
    ASTNodePtr range_{};
    ASTNodePtr body_{};
};

class ZipWithNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::ZipWith;

    explicit ZipWithNode(Location loc,
                         std::string var1,
                         std::string var2,
                         ASTNodePtr range1,
                         ASTNodePtr range2,
                         ASTNodePtr body) noexcept
        : ASTNode{kKind, loc, ValueType::List},
          var1_{std::move(var1)},
          var2_{std::move(var2)},
          range1_{std::move(range1)},
          range2_{std::move(range2)},
          body_{std::move(body)} {}
    ZipWithNode(ZipWithNode&&) = delete;
    ZipWithNode(ZipWithNode const&) = default;
    auto operator=(ZipWithNode&&) -> ZipWithNode& = delete;
    auto operator=(ZipWithNode const&) -> ZipWithNode& = delete;
    ~ZipWithNode() = default;

    [[nodiscard]] auto GetVariable1() const noexcept -> std::string const& {
        return var1_;
    }
    [[nodiscard]] auto GetVariable2() const noexcept -> std::string const& {
        return var2_;
    }
    [[nodiscard]] auto GetRange1() const noexcept -> ASTNodePtr const& {
        return range1_;
    }
    [[nodiscard]] auto GetRange2() const noexcept -> ASTNodePtr const& {
        return range2_;
    }
    [[nodiscard]] auto GetBody() const noexcept -> ASTNodePtr const& {
        return body_;
    }

  private:
    std::string var1_{};
    std::string var2_{};
    ASTNodePtr range1_{};
    ASTNodePtr range2_{};
    ASTNodePtr body_{};
};

class FoldLeftNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::FoldLeft;

    explicit FoldLeftNode(Location loc,
                          std::string iter_var,
                          std::string accu_var,
                          ASTNodePtr init,
                          ASTNodePtr range,
                          ASTNodePtr body) noexcept
        : ASTNode{kKind, loc, GetEvalType(init, body)},
          iter_var_{std::move(iter_var)},
          accu_var_{std::move(accu_var)},
          init_{std::move(init)},
          range_{std::move(range)},
          body_{std::move(body)} {}
    FoldLeftNode(FoldLeftNode&&) = delete;
    FoldLeftNode(FoldLeftNode const&) = default;
    auto operator=(FoldLeftNode&&) -> FoldLeftNode& = delete;
    auto operator=(FoldLeftNode const&) -> FoldLeftNode& = delete;
    ~FoldLeftNode() = default;

    [[nodiscard]] auto GetIterVar() const noexcept -> std::string const& {
        return iter_var_;
    }

    [[nodiscard]] auto GetAccuVar() const noexcept -> std::string const& {
        return accu_var_;
    }

    [[nodiscard]] auto GetInit() const noexcept -> ASTNodePtr const& {
        return init_;
    }

    [[nodiscard]] auto GetRange() const noexcept -> ASTNodePtr const& {
        return range_;
    }

    [[nodiscard]] auto GetBody() const noexcept -> ASTNodePtr const& {
        return body_;
    }

  private:
    std::string iter_var_;
    std::string accu_var_;
    ASTNodePtr init_;
    ASTNodePtr range_;
    ASTNodePtr body_;

    [[nodiscard]] static auto GetEvalType(ASTNodePtr const& init,
                                          ASTNodePtr const& body) noexcept
        -> ValueType {
        // the result is either the init value or the result of the body
        auto const type = init->EvalType();
        return type == body->EvalType() ? type : ValueType::Any;
    }
};

class UnaryOperationNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::UnaryOperation;

    enum class Type : std::uint8_t {
        Not,
        Minus,
    };
    explicit UnaryOperationNode(
        Location loc,
        Type type,
        ASTNodePtr expr,
        std::optional<ValueType> eval_type = std::nullopt) noexcept
        : ASTNode{kKind, loc, eval_type.value_or(ValueType::Any)},
          type_{type},
          expr_{std::move(expr)} {}
    UnaryOperationNode(UnaryOperationNode&&) = delete;
    UnaryOperationNode(UnaryOperationNode const&) = default;
    auto operator=(UnaryOperationNode&&) = delete;
    auto operator=(UnaryOperationNode const&) = delete;
    ~UnaryOperationNode() = default;

    [[nodiscard]] auto GetType() const noexcept -> Type { return type_; }
    [[nodiscard]] auto GetExpression() const noexcept -> ASTNodePtr const& {
        return expr_;
    }

  private:
    Type type_;
    ASTNodePtr expr_{};
};

class LookupNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Lookup;

    explicit LookupNode(Location loc,
                        ASTNodePtr index,
                        ASTNodePtr container,
                        ASTNodePtr default_expr = nullptr) noexcept
        : ASTNode{kKind, loc, ValueType::Any},
          index_{std::move(index)},
          container_{std::move(container)},
          default_{std::move(default_expr)} {}
    LookupNode(LookupNode&&) = delete;
    LookupNode(LookupNode const&) = default;
    auto operator=(LookupNode&&) = delete;
    auto operator=(LookupNode const&) = delete;
    ~LookupNode() = default;

    [[nodiscard]] auto GetIndex() const noexcept -> ASTNodePtr const& {
        return index_;
    }
    [[nodiscard]] auto GetContainer() const noexcept -> ASTNodePtr const& {
        return container_;
    }
    [[nodiscard]] auto GetDefault() const noexcept -> ASTNodePtr const& {
        return default_;
    }

  private:
    ASTNodePtr index_{};
    ASTNodePtr container_{};
    ASTNodePtr default_{};
};

// Syntactic sugar for combining nodes "and", "or", "==", and "!(==)"
// TODO(oreiche): Map to concrete nodes for serialization
class BinaryOperationNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::BinaryOperation;

    enum class Type : std::uint8_t {
        And,
        Or,
        Equal,
        Plus,
        Multiplication,
    };

    explicit BinaryOperationNode(
        Location loc,
        Type type,
        ASTNodePtr lhs,
        ASTNodePtr rhs,
        std::optional<ValueType> eval_type = std::nullopt) noexcept
        : ASTNode{kKind, loc, eval_type.value_or(ValueType::Any)},
          type_{type},
          lhs_{std::move(lhs)},
          rhs_{std::move(rhs)} {}
    BinaryOperationNode(BinaryOperationNode&&) = delete;
    BinaryOperationNode(BinaryOperationNode const&) = default;
    auto operator=(BinaryOperationNode&&) = delete;
    auto operator=(BinaryOperationNode const&) = delete;
    ~BinaryOperationNode() = default;

    [[nodiscard]] auto GetType() const noexcept -> Type { return type_; }
    [[nodiscard]] auto GetLhs() const noexcept -> ASTNodePtr const& {
        return lhs_;
    }
    [[nodiscard]] auto GetRhs() const noexcept -> ASTNodePtr const& {
        return rhs_;
    }

    [[nodiscard]] auto GetTypeName() const noexcept
        -> std::optional<std::string> {
        switch (type_) {
            case Type::And:
                return "and";
            case Type::Or:
                return "or";
            case Type::Equal:
                return "==";
            case Type::Plus: {
                auto const type = EvalType();
                if (type == ValueType::Number) {
                    return "+";
                }
                if (type == ValueType::String) {
                    return "join";
                }
                if (type == ValueType::List) {
                    return "++";
                }
                if (type == ValueType::Map) {
                    return "map_union";
                }
                return std::nullopt;
            }
            case Type::Multiplication:
                return "*";
        }
    }

  private:
    Type type_{};
    ASTNodePtr lhs_{};
    ASTNodePtr rhs_{};
};

// syntactic sugar for ["@", <repo>, <module>, <target>]
class RefNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Ref;

    explicit RefNode(Location loc, RefData data) noexcept
        : ASTNode{kKind, loc, ValueType::Ref}, data_{std::move(data)} {
        SetLiteral(true);
    }
    RefNode(RefNode&&) = delete;
    RefNode(RefNode const&) = default;
    auto operator=(RefNode&&) -> RefNode& = delete;
    auto operator=(RefNode const&) -> RefNode& = delete;
    ~RefNode() = default;

    [[nodiscard]] auto GetRefData() const noexcept -> RefData const& {
        return data_;
    }

  private:
    RefData data_;
};

inline auto MapNode::RemoveDuplicateFields(fields_t fields) -> fields_t {
    // seen only keeps views to strings from those subnodes that are not erased
    auto seen = std::unordered_set<std::string_view>{};
    seen.reserve(fields.size());
    // the latest field wins, so we have to iterate in reverse order
    for (auto& field : std::ranges::reverse_view(fields)) {
        auto const* name = Cast<StringNode const*>(field.first.get());
        if (name != nullptr and not seen.emplace(name->GetValue()).second) {
            // keys are never null, use nullptr to mark for removal
            field.first = nullptr;
        }
    }
    std::erase_if(fields,
                  [](auto const& val) -> bool { return val.first == nullptr; });
    return fields;
}

/// \brief Value of the build engine that is opaque to jstlang (e.g., an
/// artifact), only passed around and compared. Implemented by the build
/// engine; stored type-erased by the nodes of external values below.
class OpaqueValue {
  public:
    OpaqueValue() = default;
    OpaqueValue(OpaqueValue const&) = delete;
    OpaqueValue(OpaqueValue&&) = delete;
    auto operator=(OpaqueValue const&) -> OpaqueValue& = delete;
    auto operator=(OpaqueValue&&) -> OpaqueValue& = delete;
    virtual ~OpaqueValue() = default;

    /// \brief Value for JSON encoding (null for values that are not JSON).
    [[nodiscard]] virtual auto ToJson() const -> nlohmann::json = 0;

    /// \brief Identity of the value: equal values have equal identities. Must
    /// be valid UTF-8, as it is used in JSON (e.g., for deduplication).
    [[nodiscard]] virtual auto Identity() const -> std::string = 0;

    /// \brief Whether the value may be compared implicitly (e.g., for
    /// deduplication; not for names, as in the expression language).
    [[nodiscard]] virtual auto IsComparable() const -> bool = 0;

    /// \brief Human-readable representation (e.g., for error messages).
    [[nodiscard]] virtual auto ToString() const -> std::string = 0;
};

/// \brief Base of the nodes of values of the build engine (artifacts,
/// results, anonymous targets, entity names): literals of their own type, only
/// created by the evaluator (from values of the build engine), whose content is
/// opaque to jstlang. They are compared by their identity and encoded as their
/// JSON value (null if not JSON, as in the expression language).
class ExternalValueNode : public ASTNode {
  public:
    ExternalValueNode(ExternalValueNode&&) = delete;
    auto operator=(ExternalValueNode&&) -> ExternalValueNode& = delete;
    auto operator=(ExternalValueNode const&) -> ExternalValueNode& = delete;

    [[nodiscard]] auto GetValue() const noexcept
        -> std::shared_ptr<OpaqueValue const> const& {
        return value_;
    }

    /// \brief Whether node is a value of the build engine.
    [[nodiscard]] static auto IsExternalValue(ASTNode const& node) noexcept
        -> bool {
        switch (node.Kind()) {
            case NodeKind::Artifact:
            case NodeKind::Result:
            case NodeKind::AnonTarget:
            case NodeKind::EntityName:
                return true;
            default:
                return false;
        }
    }

    /// \brief The node as value of the build engine, nullptr if it is not.
    [[nodiscard]] static auto From(ASTNode const* node) noexcept
        -> ExternalValueNode const* {
        if (node == nullptr or not IsExternalValue(*node)) {
            return nullptr;
        }
        // the kind tag ensures the node has the derived type
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
        return static_cast<ExternalValueNode const*>(node);
    }

  protected:
    explicit ExternalValueNode(
        NodeKind kind,
        ValueType type,
        Location loc,
        std::shared_ptr<OpaqueValue const> value) noexcept
        : ASTNode{kind, loc, type, /*truth_value=*/true},
          value_{std::move(value)} {
        SetLiteral(true);
    }
    ExternalValueNode(ExternalValueNode const&) = default;
    ~ExternalValueNode() = default;

  private:
    std::shared_ptr<OpaqueValue const> value_;
};

/// \brief Artifact (e.g., of the artifacts or runfiles of a target).
class ArtifactNode final : public ExternalValueNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Artifact;

    explicit ArtifactNode(Location loc,
                          std::shared_ptr<OpaqueValue const> value) noexcept
        : ExternalValueNode{kKind, ValueType::Artifact, loc, std::move(value)} {
    }
};

/// \brief Result of the analysis of a target.
class ResultNode final : public ExternalValueNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Result;

    explicit ResultNode(Location loc,
                        std::shared_ptr<OpaqueValue const> value) noexcept
        : ExternalValueNode{kKind, ValueType::Result, loc, std::move(value)} {}
};

/// \brief Anonymous target, i.e., a target node (an abstract node or a value
/// node).
class AnonTargetNode final : public ExternalValueNode {
  public:
    static constexpr NodeKind kKind = NodeKind::AnonTarget;

    explicit AnonTargetNode(Location loc,
                            std::shared_ptr<OpaqueValue const> value) noexcept
        : ExternalValueNode{kKind,
                            ValueType::AnonTarget,
                            loc,
                            std::move(value)} {}
};

/// \brief Name of an entity (e.g., of a target or a rule).
class EntityNameNode final : public ExternalValueNode {
  public:
    static constexpr NodeKind kKind = NodeKind::EntityName;

    explicit EntityNameNode(Location loc,
                            std::shared_ptr<OpaqueValue const> value) noexcept
        : ExternalValueNode{kKind,
                            ValueType::EntityName,
                            loc,
                            std::move(value)} {}
};

inline auto ASTNode::ToVariant() const noexcept -> ASTNodeVariant {
    switch (kind_) {
        case NodeKind::Let:
            return Cast<LetNode const*>(this);
        case NodeKind::Func:
            return Cast<FuncNode const*>(this);
        case NodeKind::Builtin:
            return Cast<BuiltinNode const*>(this);
        case NodeKind::BuiltinCall:
            return Cast<BuiltinCallNode const*>(this);
        case NodeKind::Call:
            return Cast<CallNode const*>(this);
        case NodeKind::Foreign:
            return Cast<ForeignNode const*>(this);
        case NodeKind::List:
            return Cast<ListNode const*>(this);
        case NodeKind::Map:
            return Cast<MapNode const*>(this);
        case NodeKind::Bool:
            return Cast<BoolNode const*>(this);
        case NodeKind::String:
            return Cast<StringNode const*>(this);
        case NodeKind::Number:
            return Cast<NumberNode const*>(this);
        case NodeKind::Null:
            return Cast<NullNode const*>(this);
        case NodeKind::Var:
            return Cast<VarNode const*>(this);
        case NodeKind::If:
            return Cast<IfNode const*>(this);
        case NodeKind::ForEach:
            return Cast<ForEachNode const*>(this);
        case NodeKind::ZipWith:
            return Cast<ZipWithNode const*>(this);
        case NodeKind::FoldLeft:
            return Cast<FoldLeftNode const*>(this);
        case NodeKind::UnaryOperation:
            return Cast<UnaryOperationNode const*>(this);
        case NodeKind::Lookup:
            return Cast<LookupNode const*>(this);
        case NodeKind::BinaryOperation:
            return Cast<BinaryOperationNode const*>(this);
        case NodeKind::Ref:
            return Cast<RefNode const*>(this);
        case NodeKind::Closure:
            return Cast<ClosureNode const*>(this);
        case NodeKind::Artifact:
            return Cast<ArtifactNode const*>(this);
        case NodeKind::Result:
            return Cast<ResultNode const*>(this);
        case NodeKind::AnonTarget:
            return Cast<AnonTargetNode const*>(this);
        case NodeKind::EntityName:
            return Cast<EntityNameNode const*>(this);
    }
    return Cast<RefNode const*>(this);  // unreachable
}

/// \brief Function value: a function together with the scope it was defined
/// in. Only created by the evaluator, never by the parser.
class ClosureNode : public ASTNode {
  public:
    static constexpr NodeKind kKind = NodeKind::Closure;

    explicit ClosureNode(Location loc,
                         std::shared_ptr<FuncNode const> func,
                         ScopePtr scope) noexcept
        : ASTNode{kKind, loc, ValueType::Func},
          func_{std::move(func)},
          scope_{std::move(scope)} {}
    ClosureNode(ClosureNode&&) = delete;
    ClosureNode(ClosureNode const&) = default;
    auto operator=(ClosureNode&&) -> ClosureNode& = delete;
    auto operator=(ClosureNode const&) -> ClosureNode& = delete;
    ~ClosureNode() = default;

    [[nodiscard]] auto GetFunc() const noexcept -> FuncNode const& {
        return *func_;
    }

    [[nodiscard]] auto GetScope() const noexcept -> ScopePtr const& {
        return scope_;
    }

  private:
    std::shared_ptr<FuncNode const> func_;
    ScopePtr scope_;
};

}  // namespace jstlang

#endif
