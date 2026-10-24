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

#include "src/buildtool/file_system/file_root.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/jstlang/preproc/preprocessor.hpp"

namespace Frontend {

class Processor {
  public:
    explicit Processor(jstlang::FileReader file_reader,
                       jstlang::Preprocessor::logger_t logger)
        : lang_proc_{
              std::make_unique<jstlang::Preprocessor>(std::move(file_reader),
                                                      std::move(logger))} {}

    [[nodiscard]] static auto Create(gsl::not_null<FileRoot const*> root)
        -> ProcessorPtr {
        auto const logger =
            jstlang::Preprocessor::logger_t{[](auto type, auto const& msg) {
                switch (type) {
                    case jstlang::Preprocessor::LogType::Debug:
                        Logger::Log(LogLevel::Debug, msg);
                        break;
                    case jstlang::Preprocessor::LogType::Error:
                        Logger::Log(LogLevel::Error, msg);
                        break;
                }
            }};

        // without repository bindings, imported files are always of the same
        // repository, i.e., read from this root
        auto file_reader =
            jstlang::FileReader{[root](jstlang::FileLocation const& location)
                                    -> std::optional<std::string> {
                if (auto content = root->ReadContent(location.path)) {
                    return content;
                }
                Logger::Log(LogLevel::Error,
                            "Failed reading content of file {}.",
                            location.ToString());
                return std::nullopt;
            }};

        return std::make_shared<Processor>(std::move(file_reader), logger);
    }

    [[nodiscard]] auto Process(std::string const& global_repo_name,
                               std::filesystem::path const& path,
                               std::string content,
                               jstlang::FileType file_type)
        -> std::optional<nlohmann::json> {
        // the context must outlive the AST and its uses (until serialized)
        auto context = jstlang::ASTContext{};
        if (auto ast = lang_proc_->Process(
                &context,
                jstlang::FileLocation::Create(global_repo_name, path),
                std::move(content),
                file_type)) {
            return lang_proc_->Serialize(ast, file_type);
        }
        return std::nullopt;
    }

  private:
    std::unique_ptr<jstlang::Preprocessor> lang_proc_;
};

}  // namespace Frontend

namespace {

[[nodiscard]] auto ToFileType(JustFileType type) -> jstlang::FileType {
    switch (type) {
        case JustFileType::kPlain:
            return jstlang::FileType::Plain;
        case JustFileType::kTargets:
            return jstlang::FileType::Targets;
        case JustFileType::kRules:
            return jstlang::FileType::Rules;
        case JustFileType::kExpressions:
            return jstlang::FileType::Expressions;
    }
    return jstlang::FileType::Plain;
}

}  // namespace

[[nodiscard]] auto FileRoot::ReadJstlang(std::string const& global_repo_name,
                                         std::filesystem::path const& file_path,
                                         std::string file_content,
                                         JustFileType file_type) const noexcept
    -> std::optional<nlohmann::json> {
    auto const& file_proc = file_proc_.SetOnceAndGet(
        [root = this]() { return Frontend::Processor::Create(root); });
    if (file_proc) {
        return file_proc->Process(global_repo_name,
                                  file_path,
                                  std::move(file_content),
                                  ToFileType(file_type));
    }
    return std::nullopt;
}
