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

#ifndef JUSTLANG_AST_AST_HPP
#define JUSTLANG_AST_AST_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "justlang/ast/node_variant.hpp"
#include "justlang/ref.hpp"

namespace justlang {

enum class ValueType : std::uint8_t {
    Null,
    Bool,
    Number,
    String,
    List,
    Map,
    Ref,
    Func,
    Any
};

[[nodiscard]] static inline auto TypeToString(justlang::ValueType type) noexcept
    -> std::string {
    switch (type) {
        case justlang::ValueType::Null:
            return "Null";
        case justlang::ValueType::Bool:
            return "Bool";
        case justlang::ValueType::Number:
            return "Number";
        case justlang::ValueType::String:
            return "String";
        case justlang::ValueType::List:
            return "List";
        case justlang::ValueType::Map:
            return "Map";
        case justlang::ValueType::Ref:
            return "Ref";
        case justlang::ValueType::Func:
            return "Func";
        case justlang::ValueType::Any:
            return "Any";
    }
    return "";  // unreachable
}

class ASTNode;
using ASTNodePtr = std::shared_ptr<ASTNode>;

struct Location {
    std::string file;
    std::size_t line;
    std::size_t column;
    std::size_t column_end;
    std::shared_ptr<std::string> content{};
};

class ASTNode {
    template <typename... TCallable>
    class TInPlaceVisitor : public TCallable... {
      public:
        explicit TInPlaceVisitor(TCallable&&... args)
            : TCallable(std::forward<TCallable>(args))... {}
        using TCallable::operator()...;
    };

  public:
    explicit ASTNode(Location loc,
                     ValueType value_type,
                     std::optional<bool> truth_value = std::nullopt) noexcept
        : loc_{std::move(loc)},
          value_type_{value_type},
          truth_value_{truth_value} {}
    ASTNode(ASTNode&&) = delete;
    ASTNode(ASTNode const&) = default;
    auto operator=(ASTNode&&) -> ASTNode& = default;
    auto operator=(ASTNode const&) -> ASTNode& = delete;
    virtual ~ASTNode() = default;

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

    [[nodiscard]] virtual auto ToVariant() const noexcept -> ASTNodeVariant = 0;

    template <std::convertible_to<ASTNodeVariant> TNode,
              typename TUnderlying = std::remove_pointer_t<TNode>>
        requires(std::is_const_v<TUnderlying>)
    [[nodiscard]] static auto Cast(ASTNode const* node) -> TNode {
        if (node == nullptr) {
            return nullptr;
        }

        TInPlaceVisitor visitor{
            [](TNode node) -> TNode { return node; },
            [](auto /*unused*/) -> TNode { return nullptr; }};
        return std::visit(std::move(visitor), node->ToVariant());
    }

  private:
    Location loc_;
    ValueType value_type_;
    std::optional<bool> truth_value_;
};

class LetNode : public ASTNode {
  public:
    explicit LetNode(Location loc,
                     std::string name,
                     ASTNodePtr value,
                     ASTNodePtr next) noexcept
        : ASTNode{std::move(loc), next->EvalType()},
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

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    std::string name_;
    ASTNodePtr value_;
    ASTNodePtr next_;
};

// function definition
class FuncNode : public ASTNode {
  public:
    using param_t = std::pair<std::string, ASTNodePtr>;
    using params_t = std::vector<param_t>;

    explicit FuncNode(Location loc, params_t params, ASTNodePtr body) noexcept
        : ASTNode{std::move(loc), ValueType::Func},
          params_{std::move(params)},
          body_{std::move(body)} {}

    [[nodiscard]] auto GetParams() const noexcept -> params_t const& {
        return params_;
    }

    [[nodiscard]] auto GetBody() const noexcept -> ASTNodePtr const& {
        return body_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    params_t params_;
    ASTNodePtr body_;
};

class BuiltinNode : public ASTNode {
  public:
    explicit BuiltinNode(std::string name) noexcept
        : ASTNode{{}, ValueType::Func}, name_{std::move(name)} {}

    [[nodiscard]] auto GetName() const noexcept -> std::string const& {
        return name_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    std::string name_;
};

// call to function (or read of variable, if no params)
class CallNode : public ASTNode {
  public:
    using param_t = std::pair<std::optional<std::string>, ASTNodePtr>;
    using params_t = std::vector<param_t>;

    using identifier_t = std::string;
    using expr_t = ASTNodePtr;
    using builtin_t = std::shared_ptr<BuiltinNode>;
    using target_t = std::variant<identifier_t, expr_t, builtin_t>;

    explicit CallNode(Location loc,
                      target_t target,
                      std::optional<params_t> params = std::nullopt) noexcept
        : ASTNode{std::move(loc), ValueType::Any},
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

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    target_t target_;
    std::optional<params_t> params_;
};

// foreign self-contained AST (without access to upper scope)
class ForeignNode : public ASTNode {
  public:
    explicit ForeignNode(Location loc, ASTNodePtr expr) noexcept
        : ASTNode{std::move(loc), expr->EvalType()}, expr_{std::move(expr)} {}

    [[nodiscard]] auto GetExpr() const noexcept -> ASTNodePtr const& {
        return expr_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    ASTNodePtr expr_;
};

// Verbatim means that the AST is not allowed to contain calls to builtin
// functions, it must expand to a piece of literal JSON. This is important
// for certain parts of a TARGETS/RULE/EXPRESSIONS file that are not
// evaluated. Examples are:
//  - for all targets, fields "type" and "arguments_config"
//  - for export targets, fields "fixed_config", "doc", and "config_doc"
enum class VerbatimType : std::uint8_t {
    None,  ///< Do not do any verbatim treatment.
    Flat,  ///< Treat the wrapped AST node as verbatim, but not its children.
    Full   ///< Treat the wrapped AST node and all its children as verbatim.
};

class VerbatimNode : public ASTNode {
  public:
    explicit VerbatimNode(
        Location loc,
        ASTNodePtr expr,
        VerbatimType type,
        std::optional<ValueType> eval_type = std::nullopt) noexcept
        : ASTNode{std::move(loc), eval_type.value_or(expr->EvalType())},
          expr_{std::move(expr)},
          type_{type} {}
    VerbatimNode(VerbatimNode&&) = delete;
    VerbatimNode(VerbatimNode const&) = default;
    auto operator=(VerbatimNode&&) -> VerbatimNode& = delete;
    auto operator=(VerbatimNode const&) -> VerbatimNode& = delete;
    ~VerbatimNode() override = default;

    [[nodiscard]] auto GetExpr() const noexcept -> ASTNodePtr const& {
        return expr_;
    }

    [[nodiscard]] auto GetType() const noexcept -> VerbatimType {
        return type_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    ASTNodePtr expr_;
    VerbatimType type_;
};

class ListNode : public ASTNode {
  public:
    using items_t = std::vector<ASTNodePtr>;

    explicit ListNode(Location loc, items_t items) noexcept
        : ASTNode{std::move(loc), ValueType::List, not items.empty()},
          items_{std::move(items)} {}
    ListNode(ListNode&&) = delete;
    ListNode(ListNode const&) = default;
    auto operator=(ListNode&&) -> ListNode& = delete;
    auto operator=(ListNode const&) -> ListNode& = delete;
    ~ListNode() override = default;

    [[nodiscard]] auto GetItems() const noexcept -> items_t const& {
        return items_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    items_t items_{};
};

class MapNode : public ASTNode {
  public:
    using entry_t = std::pair<ASTNodePtr, ASTNodePtr>;
    using fields_t = std::vector<entry_t>;

    explicit MapNode(Location loc, fields_t fields) noexcept
        : ASTNode{std::move(loc), ValueType::Map, not fields.empty()},
          fields_{std::move(fields)} {}
    MapNode(MapNode&&) = delete;
    MapNode(MapNode const&) = default;
    auto operator=(MapNode&&) -> MapNode& = delete;
    auto operator=(MapNode const&) -> MapNode& = delete;
    ~MapNode() override = default;

    [[nodiscard]] auto GetFields() const noexcept -> fields_t const& {
        return fields_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    fields_t fields_{};
};

class BoolNode : public ASTNode {
  public:
    explicit BoolNode(Location loc, bool value) noexcept
        : ASTNode{std::move(loc), ValueType::Bool, value}, value_{value} {}
    BoolNode(BoolNode&&) = delete;
    BoolNode(BoolNode const&) = default;
    auto operator=(BoolNode&&) -> BoolNode& = delete;
    auto operator=(BoolNode const&) -> BoolNode& = delete;
    ~BoolNode() override = default;

    [[nodiscard]] auto GetValue() const noexcept -> bool { return value_; }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    bool value_{};
};

class StringNode : public ASTNode {
  public:
    explicit StringNode(Location loc, std::string value) noexcept
        : ASTNode{std::move(loc), ValueType::String, not value.empty()},
          value_{std::move(value)} {}
    StringNode(StringNode&&) = delete;
    StringNode(StringNode const&) = default;
    auto operator=(StringNode&&) -> StringNode& = delete;
    auto operator=(StringNode const&) -> StringNode& = delete;
    ~StringNode() override = default;

    [[nodiscard]] auto GetValue() const noexcept -> std::string const& {
        return value_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    std::string value_{};
};

class NumberNode : public ASTNode {
  public:
    explicit NumberNode(Location loc, double value) noexcept
        : ASTNode{std::move(loc), ValueType::Number, value != 0.0},
          value_{value} {}

    NumberNode(NumberNode&&) = delete;
    NumberNode(NumberNode const&) = default;
    auto operator=(NumberNode&&) = delete;
    auto operator=(NumberNode const&) = delete;
    ~NumberNode() override = default;

    [[nodiscard]] auto GetValue() const noexcept -> double { return value_; }
    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    double value_ = 0.;
};

class NullNode : public ASTNode {
  public:
    explicit NullNode(Location loc) noexcept
        : ASTNode{std::move(loc), ValueType::Null, /*truth_value=*/false} {}
    NullNode(NullNode&&) = delete;
    NullNode(NullNode const&) = default;
    auto operator=(NullNode&&) -> NullNode& = delete;
    auto operator=(NullNode const&) -> NullNode& = delete;
    ~NullNode() override = default;

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }
};

class VarNode : public ASTNode {
  public:
    explicit VarNode(Location loc,
                     std::string name,
                     ASTNodePtr default_expr = nullptr) noexcept
        : ASTNode{std::move(loc), ValueType::Any},
          name_{std::move(name)},
          default_{std::move(default_expr)} {}
    VarNode(VarNode&&) = delete;
    VarNode(VarNode const&) = default;
    auto operator=(VarNode&&) -> VarNode& = delete;
    auto operator=(VarNode const&) -> VarNode& = delete;
    ~VarNode() override = default;

    [[nodiscard]] auto GetName() const noexcept -> std::string const& {
        return name_;
    }
    [[nodiscard]] auto GetDefault() const noexcept -> ASTNodePtr const& {
        return default_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    std::string name_{};
    ASTNodePtr default_{};
};

class IfNode : public ASTNode {
  public:
    explicit IfNode(Location loc,
                    ASTNodePtr cond,
                    ASTNodePtr then_expr = nullptr,
                    ASTNodePtr else_expr = nullptr) noexcept
        : ASTNode{std::move(loc),
                  GetEvalType(then_expr, else_expr),
                  GetTruthValue(then_expr, else_expr)},
          cond_{std::move(cond)},
          then_{std::move(then_expr)},
          else_{std::move(else_expr)} {}
    IfNode(IfNode&&) = delete;
    IfNode(IfNode const&) = default;
    auto operator=(IfNode&&) -> IfNode& = delete;
    auto operator=(IfNode const&) -> IfNode& = delete;
    ~IfNode() override = default;

    [[nodiscard]] auto GetCondition() const noexcept -> ASTNodePtr const& {
        return cond_;
    }
    [[nodiscard]] auto GetThenBranch() const noexcept -> ASTNodePtr const& {
        return then_;
    }
    [[nodiscard]] auto GetElseBranch() const noexcept -> ASTNodePtr const& {
        return else_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
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
    explicit ForEachNode(Location loc,
                         std::string var,
                         ASTNodePtr range,
                         ASTNodePtr body) noexcept
        : ASTNode{std::move(loc), ValueType::List},
          var_{std::move(var)},
          range_{std::move(range)},
          body_{std::move(body)} {}
    ForEachNode(ForEachNode&&) = delete;
    ForEachNode(ForEachNode const&) = default;
    auto operator=(ForEachNode&&) -> ForEachNode& = delete;
    auto operator=(ForEachNode const&) -> ForEachNode& = delete;
    ~ForEachNode() override = default;

    [[nodiscard]] auto GetVariable() const noexcept -> std::string const& {
        return var_;
    }
    [[nodiscard]] auto GetRange() const noexcept -> ASTNodePtr const& {
        return range_;
    }
    [[nodiscard]] auto GetBody() const noexcept -> ASTNodePtr const& {
        return body_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    std::string var_{};
    ASTNodePtr range_{};
    ASTNodePtr body_{};
};

class ZipWithNode : public ASTNode {
  public:
    explicit ZipWithNode(Location loc,
                         std::string var1,
                         std::string var2,
                         ASTNodePtr range1,
                         ASTNodePtr range2,
                         ASTNodePtr body) noexcept
        : ASTNode{std::move(loc), ValueType::List},
          var1_{std::move(var1)},
          var2_{std::move(var2)},
          range1_{std::move(range1)},
          range2_{std::move(range2)},
          body_{std::move(body)} {}
    ZipWithNode(ZipWithNode&&) = delete;
    ZipWithNode(ZipWithNode const&) = default;
    auto operator=(ZipWithNode&&) -> ZipWithNode& = delete;
    auto operator=(ZipWithNode const&) -> ZipWithNode& = delete;
    ~ZipWithNode() override = default;

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

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
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
    explicit FoldLeftNode(Location loc,
                          std::string iter_var,
                          std::string accu_var,
                          ASTNodePtr init,
                          ASTNodePtr range,
                          ASTNodePtr body) noexcept
        : ASTNode{std::move(loc), init->EvalType()},
          iter_var_{std::move(iter_var)},
          accu_var_{std::move(accu_var)},
          init_{std::move(init)},
          range_{std::move(range)},
          body_{std::move(body)} {}
    FoldLeftNode(FoldLeftNode&&) = delete;
    FoldLeftNode(FoldLeftNode const&) = default;
    auto operator=(FoldLeftNode&&) -> FoldLeftNode& = delete;
    auto operator=(FoldLeftNode const&) -> FoldLeftNode& = delete;
    ~FoldLeftNode() override = default;

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

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    std::string iter_var_;
    std::string accu_var_;
    ASTNodePtr init_;
    ASTNodePtr range_;
    ASTNodePtr body_;
};

class UnaryOperationNode : public ASTNode {
  public:
    enum class Type : std::uint8_t {
        Not,
        Minus,
    };
    explicit UnaryOperationNode(
        Location loc,
        Type type,
        ASTNodePtr expr,
        std::optional<ValueType> eval_type = std::nullopt) noexcept
        : ASTNode{std::move(loc), eval_type.value_or(ValueType::Any)},
          type_{type},
          expr_{std::move(expr)} {}
    UnaryOperationNode(UnaryOperationNode&&) = delete;
    UnaryOperationNode(UnaryOperationNode const&) = default;
    auto operator=(UnaryOperationNode&&) = delete;
    auto operator=(UnaryOperationNode const&) = delete;
    ~UnaryOperationNode() override = default;

    [[nodiscard]] auto GetType() const noexcept -> Type { return type_; }
    [[nodiscard]] auto GetExpression() const noexcept -> ASTNodePtr const& {
        return expr_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    Type type_;
    ASTNodePtr expr_{};
};

class LookupNode : public ASTNode {
  public:
    explicit LookupNode(Location loc,
                        ASTNodePtr index,
                        ASTNodePtr container,
                        ASTNodePtr default_expr = nullptr) noexcept
        : ASTNode{std::move(loc), ValueType::Any},
          index_{std::move(index)},
          container_{std::move(container)},
          default_{std::move(default_expr)} {}
    LookupNode(LookupNode&&) = delete;
    LookupNode(LookupNode const&) = default;
    auto operator=(LookupNode&&) = delete;
    auto operator=(LookupNode const&) = delete;
    ~LookupNode() override = default;

    [[nodiscard]] auto GetIndex() const noexcept -> ASTNodePtr const& {
        return index_;
    }
    [[nodiscard]] auto GetContainer() const noexcept -> ASTNodePtr const& {
        return container_;
    }
    [[nodiscard]] auto GetDefault() const noexcept -> ASTNodePtr const& {
        return default_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
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
        : ASTNode{std::move(loc), eval_type.value_or(ValueType::Any)},
          type_{type},
          lhs_{std::move(lhs)},
          rhs_{std::move(rhs)} {}
    BinaryOperationNode(BinaryOperationNode&&) = delete;
    BinaryOperationNode(BinaryOperationNode const&) = default;
    auto operator=(BinaryOperationNode&&) = delete;
    auto operator=(BinaryOperationNode const&) = delete;
    ~BinaryOperationNode() override = default;

    [[nodiscard]] auto GetType() const noexcept -> Type { return type_; }
    [[nodiscard]] auto GetLhs() const noexcept -> ASTNodePtr const& {
        return lhs_;
    }
    [[nodiscard]] auto GetRhs() const noexcept -> ASTNodePtr const& {
        return rhs_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
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
    explicit RefNode(Location loc, RefData data) noexcept
        : ASTNode{std::move(loc), ValueType::Ref}, data_{std::move(data)} {}
    RefNode(RefNode&&) = delete;
    RefNode(RefNode const&) = default;
    auto operator=(RefNode&&) -> RefNode& = delete;
    auto operator=(RefNode const&) -> RefNode& = delete;
    ~RefNode() override = default;

    [[nodiscard]] auto GetRefData() const noexcept -> RefData const& {
        return data_;
    }

    [[nodiscard]] auto ToVariant() const noexcept -> ASTNodeVariant final {
        return this;
    }

  private:
    RefData data_;
};

}  // namespace justlang

#endif
