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

#include "justlang/ast/to_string_visitor.hpp"

#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>  // IWYU pragma: keep
#include <nlohmann/json_fwd.hpp>

#include "justlang/ast/ast.hpp"
#include "justlang/ast/to_json_visitor.hpp"

namespace {

auto const kEmptyList =
    std::make_shared<justlang::ListNode>(justlang::Location{},
                                         justlang::ListNode::items_t{});

[[nodiscard]] auto IndentString(std::size_t indent) -> std::string {
    auto str = std::string(indent * 2, ' ');
    return str;
}

[[nodiscard]] auto ToString(justlang::VerbatimType type) -> std::string {
    switch (type) {
        case justlang::VerbatimType::None:
            return "None";
        case justlang::VerbatimType::Flat:
            return "Flat";
        case justlang::VerbatimType::Full:
            return "Full";
    }
    return {};
}

}  // namespace

namespace justlang {

auto ASTToStringVisitor::Dump(ASTNode const& node) const -> std::string {
    return std::visit(*this, node.ToVariant());
}

auto ASTToStringVisitor::operator()(LetNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "LET\n";
    oss << space << "- NAME: " << node->GetName() << "\n";
    oss << space << "- VALUE:\n";
    oss << ASTToStringVisitor{indent_ + 2}.Dump(*node->GetValue());
    oss << space << "- NEXT:\n";
    oss << ASTToStringVisitor{indent_ + 2}.Dump(*node->GetNext());
    return oss.str();
}

auto ASTToStringVisitor::operator()(FuncNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "FUNC\n";
    for (auto const& [pname, pexpr] : node->GetParams()) {
        oss << space << "- " << pname << ":\n";
        oss << (pexpr ? ASTToStringVisitor{indent_ + 2}.Dump(*pexpr)
                      : std::string{"null\n"});
    }
    oss << space << "- BODY:\n";
    oss << ASTToStringVisitor{indent_ + 2}.Dump(*node->GetBody());
    return oss.str();
}

auto ASTToStringVisitor::operator()(BuiltinNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "BUILTIN(" << node->GetName() << ")\n";
    return oss.str();
}

auto ASTToStringVisitor::operator()(CallNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space
        << (node->IsVarRead() ? std::string{"READ\n"} : std::string{"CALL\n"});
    oss << space << "- TARGET:";
    auto const& target = node->GetTarget();
    if (std::holds_alternative<CallNode::identifier_t>(target)) {
        oss << " \"" << std::get<CallNode::identifier_t>(target) << "\"\n";
    }
    else if (std::holds_alternative<CallNode::expr_t>(target)) {
        oss << "\n"
            << ASTToStringVisitor{indent_ + 2}.Dump(
                   *std::get<CallNode::expr_t>(target));
    }
    else if (std::holds_alternative<CallNode::builtin_t>(target)) {
        oss << "\n"
            << ASTToStringVisitor{indent_ + 2}.Dump(
                   *std::get<CallNode::builtin_t>(target));
    }
    else {
        oss << " <UNKNOWN>\n";
    }
    if (auto params = node->GetParams()) {
        int num{};
        for (auto const& [pname, pexpr] : *params) {
            oss << space << "- ";
            if (pname) {
                oss << *pname;
            }
            else {
                oss << "<arg" << num << ">";
            }
            oss << ":\n";
            oss << ASTToStringVisitor{indent_ + 2}.Dump(*pexpr);
            ++num;
        }
    }
    return oss.str();
}

auto ASTToStringVisitor::operator()(ForeignNode const* const node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "FOREIGN\n";
    oss << ASTToStringVisitor{indent_ + 1}.Dump(*node->GetExpr());
    return oss.str();
}

auto ASTToStringVisitor::operator()(VerbatimNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "VERBATIM(" << ToString(node->GetType()) << ")\n";
    oss << ASTToStringVisitor(indent_ + 1).Dump(*node->GetExpr());
    return oss.str();
}

auto ASTToStringVisitor::operator()(ListNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "[\n";
    ASTToStringVisitor const items_visitor(indent_ + 1);
    for (auto const& item : node->GetItems()) {
        oss << items_visitor.Dump(*item);
    }
    oss << space << "]\n";
    return oss.str();
}

auto ASTToStringVisitor::operator()(MapNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "MAP\n";

    ASTToStringVisitor const field_visitor(indent_ + 2);
    int num = 0;
    for (auto const& [key, value] : node->GetFields()) {
        oss << space << "- KEY" << num << ":\n";
        oss << field_visitor.Dump(*key);
        oss << space << "- VAL" << num << ":\n";
        oss << field_visitor.Dump(*value);
        ++num;
    }
    return oss.str();
}

auto ASTToStringVisitor::operator()(BoolNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "BOOL(" << (node->GetValue() ? "true" : "false") << ")\n";
    return oss.str();
}

auto ASTToStringVisitor::operator()(StringNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "STRING(" << node->GetValue() << ")\n";
    return oss.str();
}

auto ASTToStringVisitor::operator()(NumberNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "NUMBER(" << node->GetValue() << ")\n";
    return oss.str();
}

auto ASTToStringVisitor::operator()(NullNode const* /*node*/) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "NULL\n";
    return oss.str();
}

auto ASTToStringVisitor::operator()(VarNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "VAR\n";
    oss << space << "- NAME: " << node->GetName() << "\n";
    if (node->GetDefault() != nullptr) {
        oss << space << "- DEFAULT:\n";
        oss << ASTToStringVisitor(indent_ + 2).Dump(*node->GetDefault());
    }
    return oss.str();
}

auto ASTToStringVisitor::operator()(IfNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "IF\n";
    oss << space << "- COND:\n";

    ASTToStringVisitor const if_visitor(indent_ + 2);
    oss << if_visitor.Dump(*node->GetCondition());
    if (node->GetThenBranch() != nullptr) {
        oss << space << "- THEN:\n";
        oss << if_visitor.Dump(*node->GetThenBranch());
    }
    if (node->GetElseBranch()) {
        oss << space << "- ELSE:\n";
        oss << if_visitor.Dump(*node->GetElseBranch());
    }
    return oss.str();
}

auto ASTToStringVisitor::operator()(ForEachNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "FOREACH\n";
    oss << space << "- VAR: " << node->GetVariable() << "\n";
    ASTToStringVisitor const foreach_visitor(indent_ + 2);
    oss << space << "- RANGE:\n";
    oss << foreach_visitor.Dump(*node->GetRange());
    oss << space << "- BODY:\n";
    oss << foreach_visitor.Dump(*node->GetBody());
    return oss.str();
}

auto ASTToStringVisitor::operator()(ZipWithNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "ZIP_WITH\n";
    oss << space << "- VAR1: " << node->GetVariable1() << "\n";
    oss << space << "- VAR2: " << node->GetVariable2() << "\n";
    ASTToStringVisitor const foreach_visitor(indent_ + 2);
    oss << space << "- RANGE1:\n";
    oss << foreach_visitor.Dump(*node->GetRange1());
    oss << space << "- RANGE2:\n";
    oss << foreach_visitor.Dump(*node->GetRange2());
    oss << space << "- BODY:\n";
    oss << foreach_visitor.Dump(*node->GetBody());
    return oss.str();
}

auto ASTToStringVisitor::operator()(FoldLeftNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "FOLDL\n";
    oss << space << "- ITER_VAR: " << node->GetIterVar() << "\n";
    oss << space << "- ACCU_VAR: " << node->GetAccuVar() << "\n";
    ASTToStringVisitor const fold_visitor(indent_ + 2);
    oss << space << "- INIT:\n";
    oss << fold_visitor.Dump(*node->GetInit());
    oss << space << "- RANGE:\n";
    oss << fold_visitor.Dump(*node->GetRange());
    oss << space << "- BODY:\n";
    oss << fold_visitor.Dump(*node->GetBody());
    return oss.str();
}

auto ASTToStringVisitor::operator()(UnaryOperationNode const* node) const
    -> std::string {
    using Type = UnaryOperationNode::Type;
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    switch (node->GetType()) {
        case Type::Not:
            oss << space << "NOT\n";
            break;
        case Type::Minus:
            oss << space << "NEGATE\n";
            break;
    }
    oss << ASTToStringVisitor(indent_ + 1).Dump(*node->GetExpression());
    return oss.str();
}

auto ASTToStringVisitor::operator()(LookupNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "UNRESOLVED_LOOKUP\n";
    oss << space << "- INDEX:\n";

    ASTToStringVisitor const lookup_visitor(indent_ + 2);
    oss << lookup_visitor.Dump(*node->GetIndex());
    oss << space << "- CONTAINER:\n";
    oss << lookup_visitor.Dump(*node->GetContainer());
    if (node->GetDefault() != nullptr) {
        oss << space << "- DEFAULT:\n";
        oss << lookup_visitor.Dump(*node->GetDefault());
    }
    return oss.str();
}

auto ASTToStringVisitor::operator()(BinaryOperationNode const* node) const
    -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};

    oss << space << "BINARY_OPERATION("
        << node->GetTypeName().value_or("unknown") << ")\n";
    oss << space << "- LHS:\n";

    ASTToStringVisitor const bool_visitor(indent_ + 2);
    oss << bool_visitor.Dump(*node->GetLhs());
    oss << space << "- RHS:\n";
    oss << bool_visitor.Dump(*node->GetRhs());
    return oss.str();
}

auto ASTToStringVisitor::operator()(RefNode const* node) const -> std::string {
    auto const space = IndentString(indent_);
    std::ostringstream oss{};
    oss << space << "REF(" << ASTToJsonVisitor{}(node).dump() << ")\n";
    return oss.str();
}

}  // namespace justlang
