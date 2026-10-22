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

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/expression/configuration.hpp"
#include "src/buildtool/build_engine/expression/expression.hpp"
#include "src/buildtool/build_engine/expression/jstlang_evaluator.hpp"
#include "src/buildtool/jstlang/file_data.hpp"

namespace {

struct Arguments {
    std::string filename;
    nlohmann::json config = nlohmann::json::object();
};

[[noreturn]] void PrintUsage(char const* toolname,
                             std::string const& error = "") {
    if (not error.empty()) {
        std::cerr << error << "\n\n";
    }
    std::cerr << "Usage: " << toolname << " [OPTIONS] FILENAME\n"
              << "\n"
              << "Evaluate a Jstlang file (or stdin, if FILENAME is '-').\n"
              << "\n"
              << "OPTIONS:\n"
              << "  --help             This help message\n"
              << "  -c, --config PATH  Configuration file (JSON object)\n"
              << "  -D, --defines JSON Overlay configuration (JSON object);\n"
              << "                     multiple options overlay\n"
              << "\n";
    std::exit(EXIT_FAILURE);
}

template <class TIStream>
[[nodiscard]] auto ReadData(TIStream& istream) -> std::string {
    std::stringstream input;
    input << istream.rdbuf();
    return input.str();
}

/// \brief Overlay the configuration by the given JSON object.
void Overlay(char const* toolname,
             gsl::not_null<nlohmann::json*> const& config,
             std::string const& data,
             std::string const& origin) {
    auto overlay = nlohmann::json{};
    try {
        overlay = nlohmann::json::parse(data);
    } catch (std::exception const& e) {
        PrintUsage(toolname, "Parsing " + origin + " failed: " + e.what());
    }
    if (not overlay.is_object()) {
        PrintUsage(toolname, "The " + origin + " is not a JSON object.");
    }
    config->update(overlay);
}

[[nodiscard]] auto ParseArguments(int argc, char const** argv) -> Arguments {
    auto args = Arguments{};
    auto const* toolname = argv[0];  // NOLINT
    auto filename = std::optional<std::string>{};
    auto only_positional = false;
    for (int i{1}; i < argc; ++i) {
        auto const arg = std::string_view{argv[i]};  // NOLINT
        auto const next = [&]() -> std::string {
            if (i + 1 >= argc) {
                PrintUsage(toolname,
                           "Missing value of option " + std::string{arg} + ".");
            }
            return argv[++i];  // NOLINT
        };
        if (not only_positional and arg.starts_with("-") and arg != "-") {
            if (arg == "--") {
                only_positional = true;
            }
            else if (arg == "--help") {
                PrintUsage(toolname);
            }
            else if (arg == "-c" or arg == "--config") {
                auto path = next();
                auto file = std::ifstream{path};
                if (not file.good()) {
                    PrintUsage(toolname, "Cannot read file " + path + ".");
                }
                Overlay(toolname,
                        &args.config,
                        ReadData(file),
                        "configuration file " + path);
            }
            else if (arg == "-D" or arg == "--defines") {
                Overlay(toolname, &args.config, next(), "option -D");
            }
            else {
                PrintUsage(toolname, "Unknown option " + std::string{arg});
            }
        }
        else if (filename) {
            PrintUsage(toolname, "Only one filename can be given.");
        }
        else {
            filename = std::string{arg};
        }
    }
    if (not filename) {
        PrintUsage(toolname, "Missing filename argument.");
    }
    args.filename = *filename;
    return args;
}

}  // namespace

auto main(int argc, char const** argv) -> int {
    try {
        auto const args = ParseArguments(argc, argv);

        // files (incl. imports) are read relative to the working directory,
        // which is taken as the root of the (only) repository
        auto const cwd = std::filesystem::current_path();
        auto reader =
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

        auto const is_stdin = args.filename == "-";
        auto value = JstlangEvaluator::EvaluateFile(
            reader,
            is_stdin ? jstlang::FileLocation{.repo = "", .path = "<stdin>"}
                     : jstlang::FileLocation::Create("", args.filename),
            is_stdin ? std::optional{ReadData(std::cin)} : std::nullopt,
            Configuration{Expression::FromJson(args.config)},
            [](std::string const& msg) { std::cerr << msg << "\n"; });
        if (not value) {
            return EXIT_FAILURE;
        }
        std::cout << value->ToJson().dump(2) << "\n";
        return EXIT_SUCCESS;
    } catch (std::exception const& e) {
        std::cerr << "Evaluation failed: " << e.what() << "\n";
    }
    return EXIT_FAILURE;
}
