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

#ifndef JUSTLANG_AST_NATIVE_STATIC_ERROR_HPP
#define JUSTLANG_AST_NATIVE_STATIC_ERROR_HPP

#include <ctime>
#include <exception>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

#include "justlang/ast/ast.hpp"

namespace justlang {

class CompilerException : public std::exception {
  public:
    // with Token info
    CompilerException(std::string phase, std::string message)
        : phase_{std::move(phase)},
          msg_{std::move(message)},
          loc_{std::nullopt},
          full_msg_{ToString()} {}

    CompilerException(std::string phase, std::string message, Location loc)
        : phase_{std::move(phase)},
          msg_{std::move(message)},
          loc_{std::move(loc)},
          full_msg_{ToString()} {}

    [[nodiscard]] auto what() const noexcept -> char const* override {
        return full_msg_.c_str();
    }

    [[nodiscard]] auto Phase() const noexcept -> std::string const& {
        return phase_;
    }

    [[nodiscard]] auto Message() const noexcept -> std::string const& {
        return msg_;
    }

    [[nodiscard]] auto Location() const noexcept
        -> std::optional<Location> const& {
        return loc_;
    }

    [[nodiscard]] auto HasLocation() const noexcept -> bool {
        return loc_.has_value();
    }

  private:
    std::string phase_;
    std::string msg_;
    std::optional<justlang::Location> loc_;
    std::string full_msg_;

    [[nodiscard]] auto ToString() const -> std::string {
        std::ostringstream oss;
        if (loc_) {
            if (!loc_->file.empty()) {
                oss << loc_->file;
            }
            oss << ":" << loc_->line << ":" << loc_->column;
            if (loc_->column_end != loc_->column) {
                oss << "-" << loc_->column_end;
            }
            oss << " ";
        }
        oss << phase_ << " error: " << msg_;
        return oss.str();
    }
};

class LexerException : public CompilerException {
  public:
    explicit LexerException(std::string const& message)
        : CompilerException("Lexer", message) {}
};

class ParserException : public CompilerException {
  public:
    explicit ParserException(std::string const& message,
                             justlang::Location loc = {})
        : CompilerException("Parser", message, std::move(loc)) {}
};

}  // namespace justlang

#endif  // JUSTLANG_AST_NATIVE_STATIC_ERROR_HPP
