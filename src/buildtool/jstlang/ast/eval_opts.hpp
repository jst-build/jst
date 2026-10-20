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

#ifndef INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_OPTS_HPP
#define INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_OPTS_HPP

#include <cstdint>
#include <functional>

#include "src/buildtool/jstlang/ast/ast.hpp"

namespace jstlang {

/// \brief Resolve an imported file to its AST.
using ImportResolver = std::function<ASTNodePtr(FileLocation const&)>;

/// \brief Options of an evaluation.
struct EvalOptions {
    static constexpr std::size_t kDefaultUnrollLimit = 64;

    /// \brief How values that are unknown are treated.
    enum class Mode : std::uint8_t {
        /// \brief Full evaluation, every value must be known: runtime
        /// variables are read from the configuration. Without configuration
        /// (static evaluation), reading a runtime variable is an error.
        Full,
        /// \brief Unfolding: all `local`s and functions are unfolded
        /// (inlined) and expressions with known operands are folded; unknown
        /// values (runtime variables) stay residual code in jstlang: calls of
        /// built-in functions with unknown arguments stay calls, conditions and
        /// comprehensions stay (their iteration variables are renamed to fresh
        /// names that cannot be captured), and closures are unfolded to
        /// self-contained functions. The residual code is self-contained, i.e.,
        /// it can be fully evaluated later (in a configuration) without the
        /// scope it was unfolded in.
        Unfold,
        /// \brief Lowering to the JSON expression language: like unfolding, but
        /// the residual code is code of the JSON expression language: calls of
        /// built-in functions with unknown arguments become built-in calls of
        /// the JSON expression language, and the iteration variables of
        /// comprehensions over unknown ranges become runtime variables (reading
        /// a runtime variable of the same name via env() is an error).
        Lowering,
    };

    /// \brief Value of a runtime variable, nullptr if not set.
    using ConfigLookup = std::function<ASTNodePtr(std::string const&)>;

    Mode mode{Mode::Full};
    /// \brief Configuration for full evaluation, nullptr for static
    /// evaluation.
    ConfigLookup const* config{nullptr};
    /// \brief Unfolding and lowering: unroll comprehensions over literal
    /// ranges of up to this size, i.e., evaluate them statically; larger ones
    /// stay residual comprehensions (full evaluation always unrolls). The
    /// default is a safety net against pathological inputs.
    std::size_t unroll_limit{kDefaultUnrollLimit};
    /// \brief Resolver for imports that are not resolved in the AST (imports
    /// parsed as deferred), nullptr if not available (then evaluating such an
    /// import is an error).
    ImportResolver const* imports{nullptr};
};

}  // namespace jstlang

#endif  // INCLUDED_SRC_BUILDTOOL_JSTLANG_AST_EVAL_OPTS_HPP