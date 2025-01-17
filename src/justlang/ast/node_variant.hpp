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

#ifndef JUSTLANG_AST_NODE_VARIANT_HPP
#define JUSTLANG_AST_NODE_VARIANT_HPP

#include <variant>

namespace justlang {
using ASTNodeVariant = std::variant<class LetNode const*,
                                    class FuncNode const*,
                                    class BuiltinNode const*,
                                    class CallNode const*,
                                    class ForeignNode const*,
                                    class VerbatimNode const*,
                                    class ListNode const*,
                                    class MapNode const*,
                                    class BoolNode const*,
                                    class StringNode const*,
                                    class NumberNode const*,
                                    class NullNode const*,
                                    class VarNode const*,
                                    class IfNode const*,
                                    class ForEachNode const*,
                                    class FoldLeftNode const*,
                                    class UnaryOperationNode const*,
                                    class LookupNode const*,
                                    class BinaryOperationNode const*,
                                    class RefNode const*>;
}  // namespace justlang

#endif  // JUSTLANG_AST_NODE_VARIANT_HPP
