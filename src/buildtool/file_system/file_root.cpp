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

#include "src/buildtool/file_system/file_root.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "fmt/core.h"
#include "gsl/gsl"
#include "nlohmann/json.hpp"

#ifndef BOOTSTRAP_BUILD_TOOL
#include "justlang/preprocessor.hpp"

namespace Frontend {

class Processor {
  public:
    explicit Processor(justlang::FileData::reader_t file_reader,
                       justlang::Preprocessor::logger_t logger)
        : lang_proc_{
              std::make_unique<justlang::Preprocessor>(std::move(file_reader),
                                                       std::move(logger))} {}

    [[nodiscard]] static auto Create(gsl::not_null<FileRoot const*> root)
        -> ProcessorPtr {
        auto const logger =
            justlang::Preprocessor::logger_t{[](auto type, auto const& msg) {
                switch (type) {
                    case justlang::Preprocessor::LogType::Debug:
                        Logger::Log(LogLevel::Debug, msg);
                        break;
                    case justlang::Preprocessor::LogType::Error:
                        Logger::Log(LogLevel::Error, msg);
                        break;
                }
            }};

        auto file_reader = justlang::FileData::reader_t{
            [root](
                justlang::FileLocation const& imported_from,
                std::filesystem::path const& filename,
                std::string const* repo) -> std::optional<justlang::FileData> {
                if (repo != nullptr) {
                    Logger::Log(
                        LogLevel::Error,
                        "Importing from repository bindings is not supported.");
                    return std::nullopt;
                }

                if (filename.empty()) {
                    Logger::Log(LogLevel::Error, "Cannot read empty filename.");
                    return std::nullopt;
                }

                auto full_path =
                    (filename.is_relative()
                         ? imported_from.path.parent_path() / filename
                         : filename)
                        .lexically_proximate("/")
                        .lexically_normal();

                if (auto content = root->ReadContent(full_path)) {
                    auto location = justlang::FileLocation{
                        .repo = imported_from.repo,
                        .path = std::move(full_path),
                        .content = std::make_shared<std::string>(*content)};
                    return justlang::FileData{.location = std::move(location),
                                              .content = std::move(*content)};
                }

                Logger::Log(LogLevel::Error,
                            "Failed reading content of file {}.",
                            full_path.string());

                return std::nullopt;
            }};

        return std::make_shared<Processor>(std::move(file_reader), logger);
    }

    [[nodiscard]] auto Process(std::string const& global_repo_name,
                               std::filesystem::path const& path,
                               std::string content,
                               justlang::FileType file_type)
        -> std::optional<nlohmann::json> {
        auto file_data = justlang::FileData{
            .location =
                justlang::FileLocation{.repo = global_repo_name, .path = path},
            .content = std::move(content)};
        file_data.location.content =
            std::make_shared<std::string>(file_data.content);

        if (auto ast = lang_proc_->Process(file_data, file_type)) {
            return lang_proc_->Serialize(ast);
        }

        return std::nullopt;
    }

  private:
    std::unique_ptr<justlang::Preprocessor> lang_proc_;
};

}  // namespace Frontend

namespace {

[[nodiscard]] auto ToFileType(JustFileType type) -> justlang::FileType {
    switch (type) {
        case JustFileType::kPlain:
            return justlang::FileType::Plain;
        case JustFileType::kTargets:
            return justlang::FileType::Targets;
        case JustFileType::kRules:
            return justlang::FileType::Rules;
        case JustFileType::kExpressions:
            return justlang::FileType::Expressions;
    }
    return justlang::FileType::Plain;
}

}  // namespace

#endif  // BOOTSTRAP_BUILD_TOOL

[[nodiscard]] auto FileRoot::ReadJustlang(
    std::string const& global_repo_name,
    std::filesystem::path const& file_path,
    std::string file_content,
    JustFileType file_type) const noexcept -> std::optional<nlohmann::json> {
#ifdef BOOTSTRAP_BUILD_TOOL
    std::ignore = global_repo_name;
    std::ignore = file_path;
    std::ignore = file_content;
    std::ignore = file_type;
#else
    auto const& file_proc = file_proc_.SetOnceAndGet(
        [root = this]() { return Frontend::Processor::Create(root); });
    if (file_proc) {
        return file_proc->Process(global_repo_name,
                                  file_path,
                                  std::move(file_content),
                                  ToFileType(file_type));
    }
#endif  // BOOTSTRAP_BUILD_TOOL
    return std::nullopt;
}
