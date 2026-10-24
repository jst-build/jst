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

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "src/buildtool/jstlang/file_data.hpp"
#include "src/buildtool/jstlang/preproc/preprocessor.hpp"

namespace {

auto const kDefaultRepo = std::string{};

void PrintUsage(char const* toolname, std::string const& error = "") {
    if (not error.empty()) {
        std::cerr << error << "\n\n";
    }
    std::cerr << "Usage: " << toolname << " [OPTIONS] FILENAME\n"
              << "\n"
              << "OPTIONS:\n"
              << "  --help          This help message\n"
              << "  --plain         Translate as plain data\n"
              << "  --targets       Translate as TARGETS file (default)\n"
              << "  --rules         Translate as RULES file\n"
              << "  --expressions   Translate as EXPRESSIONS file\n"
              << "\n";
    std::exit(EXIT_FAILURE);
}

[[nodiscard]] auto StartsWith(const char* str, const char* prefix) -> bool {
    return std::string_view{
               str, std::min(std::strlen(str), std::strlen(prefix))} == prefix;
}

template <class TIStream>
[[nodiscard]] auto ReadData(TIStream& istream) -> std::string {
    std::stringstream input;
    while (istream.good()) {
        std::string buffer;
        std::getline(istream, buffer);
        input << buffer << '\n';
    }
    return input.str();
}

[[nodiscard]] auto ParseArguments(int argc, const char** argv)
    -> std::pair<std::string, jstlang::FileType> {
    using namespace std::string_literals;
    char const* filename = nullptr;
    auto file_type = jstlang::FileType::Targets;
    auto only_positional = false;
    for (int i{1}; i < argc; ++i) {
        if (not only_positional and StartsWith(argv[i], "--")) {  // NOLINT
            if (std::strlen(argv[i]) == 2) {                      // NOLINT
                only_positional = true;
            }
            else if (std::string{"--help"} == argv[i]) {  // NOLINT
                PrintUsage(argv[0]);                      // NOLINT
            }
            else if (std::string{"--plain"} == argv[i]) {  // NOLINT
                file_type = jstlang::FileType::Plain;
            }
            else if (std::string{"--targets"} == argv[i]) {  // NOLINT
                file_type = jstlang::FileType::Targets;
            }
            else if (std::string{"--rules"} == argv[i]) {  // NOLINT
                file_type = jstlang::FileType::Rules;
            }
            else if (std::string{"--expression"} == argv[i]) {  // NOLINT
                file_type = jstlang::FileType::Expressions;
            }
            else {
                PrintUsage(argv[0], "Unknown option "s + argv[i]);  // NOLINT
            }
        }
        else {
            filename = argv[i];  // NOLINT
        }
    }
    if (filename == nullptr) {
        PrintUsage(argv[0], "Missing filename argument.");  // NOLINT
    }
    return {filename, file_type};
}

}  // namespace

auto main(int argc, const char** argv) -> int {
    auto [filename, file_type] = ParseArguments(argc, argv);

    auto cwd = std::filesystem::current_path();
    auto file_reader =
        jstlang::FileReader{[&cwd](jstlang::FileLocation const& location)
                                -> std::optional<std::string> {
            auto read_path = cwd / location.path;
            auto in_file = std::ifstream(read_path);
            if (not in_file.good()) {
                std::cerr << "Cannot read file " << read_path << "\n";
                return std::nullopt;
            }
            return ReadData(in_file);
        }};
    // only the default repository is available for local file processing
    auto repos = jstlang::RepoResolver{
        [](std::string const& /*repo*/,
           std::string const& name) -> std::optional<std::string> {
            if (name == kDefaultRepo) {
                return kDefaultRepo;
            }
            return std::nullopt;
        }};

    auto preproc = jstlang::Preprocessor{
        std::move(file_reader),
        [](auto /*type*/, auto const& msg) { std::cerr << msg; },
        std::move(repos)};
    // the context must outlive the AST and its uses (until serialized)
    auto context = jstlang::ASTContext{};
    auto output =
        filename == "-"
            ? preproc.Process(&context,
                              jstlang::FileLocation{.repo = kDefaultRepo,
                                                    .path = "<stdin>"},
                              ReadData(std::cin),
                              file_type)
            : preproc.Process(
                  &context,
                  jstlang::FileLocation::Create(kDefaultRepo, filename),
                  file_type);

    if (not output) {
        std::cerr << "Parsing failed.\n";
        return EXIT_FAILURE;
    }

    if (auto json = preproc.Serialize(output, file_type)) {
#ifndef NDEBUG
        std::cerr << "---- JSON AST\n";
#endif
        std::cout << json->dump(2) << "\n";
        return EXIT_SUCCESS;
    }

    return EXIT_FAILURE;
}
