// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef BOOTSTRAP_BUILD_TOOL

#include "src/buildtool/main/describe.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fmt/color.h"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "src/buildtool/build_engine/base_maps/entity_name.hpp"
#include "src/buildtool/build_engine/base_maps/module_name.hpp"
#include "src/buildtool/build_engine/base_maps/rule_map.hpp"
#include "src/buildtool/build_engine/base_maps/targets_file_map.hpp"
#include "src/buildtool/build_engine/target_map/target_map.hpp"
#include "src/buildtool/common/artifact.hpp"
#include "src/buildtool/common/artifact_digest.hpp"
#include "src/buildtool/execution_api/common/execution_api.hpp"
#include "src/buildtool/file_system/file_root.hpp"
#include "src/buildtool/file_system/object_type.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/exit_codes.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

namespace {

namespace Base = BuildMaps::Base;

// Indentation of the different levels of the description.
auto constexpr kIndentWidth = std::size_t{2};
auto constexpr kIndentEntry = "  ";
auto constexpr kIndentEntryDoc = "    ";
auto constexpr kIndentSubEntry = "    ";
auto constexpr kIndentSubEntryDoc = "      ";

auto const kStyleSection = fmt::emphasis::bold | fg(fmt::color::lime_green);
auto const kStyleName = fmt::emphasis::bold | fg(fmt::color::light_blue);
auto const kStyleVar = fmt::text_style{fg(fmt::color::orchid)};
auto const kStyleEntity = fmt::text_style{fmt::emphasis::bold};
auto const kStyleEntityType = fmt::emphasis::bold | fg(fmt::color::yellow);
auto const kStyleDim = fmt::text_style{fmt::emphasis::faint};

/// \brief Styling of the description. Structure is highlighted in green, names
/// that can be set in a targets file in blue, and configuration variables,
/// which live in a different namespace, in magenta. Decoration is dimmed and
/// documentation is left unstyled, as it is the text to actually read.
class Style {
  public:
    explicit Style(bool colored) noexcept : colored_{colored} {}

    /// \brief Header of a section, e.g., "STRING FIELDS (11)".
    [[nodiscard]] auto Section(std::string const& msg) const -> std::string {
        return Apply(kStyleSection, msg);
    }

    /// \brief Name of a field or provider, as written in a targets file.
    [[nodiscard]] auto Name(std::string const& msg) const -> std::string {
        return Apply(kStyleName, msg);
    }

    /// \brief Name of a configuration variable.
    [[nodiscard]] auto Var(std::string const& msg) const -> std::string {
        return Apply(kStyleVar, msg);
    }

    /// \brief Name of entity type, e.g., "TARGET"/"RULE"
    [[nodiscard]] auto EntityType(std::string const& msg) const -> std::string {
        return Apply(kStyleEntityType, msg);
    }

    /// \brief Name of a target, including rules and expressions.
    [[nodiscard]] auto Entity(std::string const& msg) const -> std::string {
        return Apply(kStyleEntity, msg);
    }

    /// \brief Decoration that should not attract attention.
    [[nodiscard]] auto Dim(std::string const& msg) const -> std::string {
        return Apply(kStyleDim, msg);
    }

  private:
    bool colored_;

    [[nodiscard]] auto Apply(fmt::text_style const& style,
                             std::string const& msg) const -> std::string {
        return colored_ ? fmt::format(style, "{}", msg) : msg;
    }
};

/// \brief Quote a name with single quotes, unless it is a simple identifier
/// that can be read without quotes.
[[nodiscard]] auto QuoteName(std::string const& name) -> std::string {
    auto const simple =
        not name.empty() and std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 or c == '_';
        });
    return simple ? name : fmt::format("'{}'", name);
}

/// \brief Quote the name of a target with single quotes, the way entity names
/// are reported everywhere else. Rules, including built-in ones, are targets.
[[nodiscard]] auto QuoteEntity(std::string const& name) -> std::string {
    return fmt::format("'{}'", name);
}

/// \brief Obtain the name of a target as it should be reported: resolved and
/// quoted, if it is a name, and as plain JSON otherwise.
[[nodiscard]] auto ResolveTargetName(
    nlohmann::json const& target,
    BuildMaps::Base::EntityName const& relative_to,
    gsl::not_null<const RepositoryConfig*> const& repo_config) -> std::string {
    auto resolved = BuildMaps::Base::ParseEntityNameFromJson(
        target, relative_to, repo_config, [](std::string const& /*unused*/) {});
    if (resolved) {
        return resolved->ToString();
    }
    if (target.is_string()) {
        return QuoteEntity(target.get<std::string>());
    }
    return target.dump();
}

/// \brief Obtain the keys of a JSON object as an array of names.
[[nodiscard]] auto Keys(nlohmann::json const& object) -> nlohmann::json {
    auto keys = nlohmann::json::array();
    for (auto const& el : object.items()) {
        keys.push_back(el.key());
    }
    return keys;
}

void PrintDoc(const nlohmann::json& doc, const std::string& indent) {
    if (not doc.is_array()) {
        return;
    }
    // Print a single line of documentation. Blank lines, which are used to
    // separate paragraphs, are printed without indentation, as to not leave
    // any trailing whitespace behind.
    auto const print_line = [&indent](std::string_view line) {
        if (line.find_first_not_of(" \t") == std::string_view::npos) {
            std::cout << "\n";
        }
        else {
            std::cout << indent << line << "\n";
        }
    };
    for (auto const& line : doc) {
        if (line.is_string()) {
            auto const& str = line.get<std::string>();
            std::size_t start{};
            auto end = str.find('\n', start);
            while (end != std::string::npos) {
                print_line(std::string_view{&str[start], end - start});
                start = end + 1;
                end = str.find('\n', start);
            }
            if (start < str.size()) {
                print_line(std::string_view{&str[start], str.size() - start});
            }
            else if (str.empty()) {
                print_line(std::string_view{});
            }
        }
    }
}

void PrintTargetHeader(Style const& style, std::string const& target) {
    std::cout << style.EntityType("TARGET  ") << style.Entity(target) << "\n";
}

void PrintRuleHeader(Style const& style,
                     std::string const& rule,
                     bool built_in) {
    std::cout << style.EntityType("RULE    ") << style.Entity(rule) << "  "
              << style.Dim(built_in ? "(built-in)" : "(user-defined)") << "\n";
}

void PrintDescription(nlohmann::json const& desc) {
    if (auto doc = desc.find("doc"); doc != desc.end()) {
        std::cout << "\n";
        PrintDoc(*doc, kIndentEntry);
    }
}

void PrintSection(Style const& style,
                  std::string const& title,
                  std::optional<std::size_t> count = std::nullopt) {
    std::cout << "\n"
              << style.Section(count ? fmt::format("{} ({})", title, *count)
                                     : title)
              << "\n";
}

/// \brief Print the given names only, filled into the available width.
void PrintNames(Style const& style,
                nlohmann::json const& names,
                bool is_var,
                unsigned int width) {
    std::size_t column{};
    for (auto const& entry : names) {
        if (not entry.is_string()) {
            continue;
        }
        auto const name = QuoteName(entry.get<std::string>());
        if (column == 0) {
            std::cout << kIndentEntry;
            column = kIndentWidth;
        }
        else if (column + kIndentWidth + name.size() > width) {
            std::cout << "\n" << kIndentEntry;
            column = kIndentWidth;
        }
        else {
            std::cout << kIndentEntry;
            column += kIndentWidth;
        }
        std::cout << (is_var ? style.Var(name) : style.Name(name));
        column += name.size();
    }
    if (column > 0) {
        std::cout << "\n";
    }
}

/// \brief Print the given names, each followed by its documentation.
void PrintNamesWithDoc(Style const& style,
                       nlohmann::json const& names,
                       nlohmann::json const& docs,
                       bool is_var) {
    for (auto const& entry : names) {
        if (not entry.is_string()) {
            continue;
        }
        auto const name = QuoteName(entry.get<std::string>());
        std::cout << kIndentEntry
                  << (is_var ? style.Var(name) : style.Name(name)) << "\n";
        if (auto doc = docs.find(entry); doc != docs.end()) {
            PrintDoc(*doc, kIndentEntryDoc);
        }
    }
}

void PrintNamedSection(Style const& style,
                       std::string const& title,
                       nlohmann::json const& names,
                       nlohmann::json const& docs,
                       DescribeOptions const& options,
                       bool is_var = false) {
    if (names.empty()) {
        return;
    }
    PrintSection(style, title, names.size());
    if (options.brief) {
        PrintNames(style, names, is_var, options.width);
    }
    else {
        PrintNamesWithDoc(style, names, docs, is_var);
    }
}

/// \brief Obtain the object stored under the given key, if any.
[[nodiscard]] auto GetObject(nlohmann::json const& desc,
                             std::string const& key) -> nlohmann::json {
    if (auto value = desc.find(key);
        value != desc.end() and value->is_object()) {
        return *value;
    }
    return nlohmann::json::object();
}

/// \brief Obtain the array stored under the given key, if any.
[[nodiscard]] auto GetArray(nlohmann::json const& desc,
                            std::string const& key) -> nlohmann::json {
    if (auto value = desc.find(key); value != desc.end()) {
        return *value;
    }
    return nlohmann::json::array();
}

/// \brief Print the description of an export target, which is used for targets
/// with present as well as with absent roots and hence has to be identical for
/// both.
void PrintExportDescription(Style const& style,
                            nlohmann::json const& desc,
                            DescribeOptions const& options) {
    PrintDescription(desc);
    PrintNamedSection(style,
                      "FLEXIBLE CONFIGURATION VARIABLES",
                      GetArray(desc, "flexible_config"),
                      GetObject(desc, "config_doc"),
                      options,
                      /*is_var=*/true);
}

void PrettyPrintRule(nlohmann::json const& rdesc,
                     BuildMaps::Base::EntityName const& rule_name,
                     gsl::not_null<const RepositoryConfig*> const& repo_config,
                     Style const& style,
                     DescribeOptions const& options) {
    PrintRuleHeader(style, rule_name.ToString(), /*built_in=*/false);
    PrintDescription(rdesc);

    auto const field_doc = GetObject(rdesc, "field_doc");
    PrintNamedSection(style,
                      "STRING FIELDS",
                      GetArray(rdesc, "string_fields"),
                      field_doc,
                      options);
    PrintNamedSection(style,
                      "TARGET FIELDS",
                      GetArray(rdesc, "target_fields"),
                      field_doc,
                      options);

    auto const implicit_targets = GetObject(rdesc, "implicit");
    if (not implicit_targets.empty()) {
        PrintSection(style, "IMPLICIT DEPENDENCIES", implicit_targets.size());
        if (options.brief) {
            PrintNames(
                style, Keys(implicit_targets), /*is_var=*/false, options.width);
        }
        else {
            for (auto const& [key, value] : implicit_targets.items()) {
                std::cout << kIndentEntry << style.Name(QuoteName(key)) << "\n";
                if (auto doc = field_doc.find(key); doc != field_doc.end()) {
                    PrintDoc(*doc, kIndentEntryDoc);
                }
                for (auto const& entry : value) {
                    auto resolved_entry =
                        BuildMaps::Base::ParseEntityNameFromJson(
                            entry,
                            rule_name,
                            repo_config,
                            [&entry, &rule_name](std::string const& parse_err) {
                                Logger::Log(
                                    LogLevel::Warning,
                                    "Failed to resolve {} relative to {}:\n{}",
                                    entry.dump(),
                                    rule_name.ToString(),
                                    parse_err);
                            });
                    if (resolved_entry) {
                        std::cout << kIndentEntryDoc << style.Dim("- ")
                                  << style.Entity(resolved_entry->ToString())
                                  << "\n";
                    }
                }
            }
        }
    }

    PrintNamedSection(style,
                      "CONFIG FIELDS",
                      GetArray(rdesc, "config_fields"),
                      field_doc,
                      options);
    PrintNamedSection(style,
                      "CONFIGURATION VARIABLES",
                      GetArray(rdesc, "config_vars"),
                      GetObject(rdesc, "config_doc"),
                      options,
                      /*is_var=*/true);

    auto const provides_doc = GetObject(rdesc, "provides_doc");
    if (options.brief) {
        if (not provides_doc.empty()) {
            PrintSection(style, "PROVIDERS", provides_doc.size());
            PrintNames(
                style, Keys(provides_doc), /*is_var=*/false, options.width);
        }
        std::cout << std::flush;
        return;
    }

    PrintSection(style, "RESULT");
    std::cout << kIndentEntry << style.Name("artifacts") << "\n";
    if (auto doc = rdesc.find("artifacts_doc"); doc != rdesc.end()) {
        PrintDoc(*doc, kIndentEntryDoc);
    }
    std::cout << kIndentEntry << style.Name("runfiles") << "\n";
    if (auto doc = rdesc.find("runfiles_doc"); doc != rdesc.end()) {
        PrintDoc(*doc, kIndentEntryDoc);
    }
    if (not provides_doc.empty()) {
        std::cout << kIndentEntry << style.Name("providers") << " "
                  << style.Dim(fmt::format("({})", provides_doc.size()))
                  << "\n";
        for (auto const& el : provides_doc.items()) {
            std::cout << kIndentSubEntry << style.Name(QuoteName(el.key()))
                      << "\n";
            PrintDoc(el.value(), kIndentSubEntryDoc);
        }
    }
    std::cout << std::flush;
}

void PrintRuleAsOrderedJson(nlohmann::json const& rdesc,
                            nlohmann::json const& rule_name) {
    auto string_fields = nlohmann::json::array();
    if (auto doc = rdesc.find("string_fields"); doc != rdesc.end()) {
        string_fields = *doc;
    }
    auto target_fields = nlohmann::json::array();
    if (auto doc = rdesc.find("target_fields"); doc != rdesc.end()) {
        target_fields = *doc;
    }
    auto config_fields = nlohmann::json::array();
    if (auto doc = rdesc.find("config_fields"); doc != rdesc.end()) {
        config_fields = *doc;
    }
    auto config_vars = nlohmann::json::array();
    if (auto doc = rdesc.find("config_vars"); doc != rdesc.end()) {
        config_vars = *doc;
    }

    auto field_doc = nlohmann::ordered_json::object();
    if (auto doc = rdesc.find("field_doc");
        doc != rdesc.end() and doc->is_object()) {
        auto fields = nlohmann::json::array();
        fields.insert(fields.end(), string_fields.begin(), string_fields.end());
        fields.insert(fields.end(), target_fields.begin(), target_fields.end());
        fields.insert(fields.end(), config_fields.begin(), config_fields.end());
        for (auto const& field : fields) {
            if (doc->contains(field)) {
                auto name = field.get<std::string>();
                field_doc[name] = (*doc)[name];
            }
        }
    }

    auto config_doc = nlohmann::ordered_json::object();
    if (auto doc = rdesc.find("config_doc");
        doc != rdesc.end() and doc->is_object()) {
        for (auto const& var : config_vars) {
            if (doc->contains(var)) {
                auto name = var.get<std::string>();
                config_doc[name] = (*doc)[name];
            }
        }
    }

    auto json_doc = nlohmann::ordered_json::object();
    json_doc["type"] = rule_name;
    if (auto doc = rdesc.find("doc"); doc != rdesc.end()) {
        json_doc["doc"] = *doc;
    }
    if (not string_fields.empty()) {
        json_doc["string_fields"] = string_fields;
    }
    if (not target_fields.empty()) {
        json_doc["target_fields"] = target_fields;
    }
    if (not config_fields.empty()) {
        json_doc["config_fields"] = config_fields;
    }
    if (not field_doc.empty()) {
        json_doc["field_doc"] = field_doc;
    }
    if (not config_vars.empty()) {
        json_doc["config_vars"] = config_vars;
    }
    if (not config_doc.empty()) {
        json_doc["config_doc"] = config_doc;
    }
    if (auto doc = rdesc.find("artifacts_doc"); doc != rdesc.end()) {
        json_doc["artifacts_doc"] = *doc;
    }
    if (auto doc = rdesc.find("runfiles_doc"); doc != rdesc.end()) {
        json_doc["runfiles_doc"] = *doc;
    }
    if (auto doc = rdesc.find("provides_doc"); doc != rdesc.end()) {
        json_doc["provides_doc"] = *doc;
    }
    std::cout << json_doc.dump(2) << std::endl;
}

}  // namespace

auto DescribeUserDefinedRule(
    BuildMaps::Base::EntityName const& rule_name,
    gsl::not_null<const RepositoryConfig*> const& repo_config,
    std::size_t jobs,
    DescribeOptions const& options) -> int {
    bool failed{};
    auto rule_file_map = Base::CreateRuleFileMap(repo_config, jobs);
    nlohmann::json rules_file;
    {
        TaskSystem ts{jobs};
        rule_file_map.ConsumeAfterKeysReady(
            &ts,
            {rule_name.ToModule()},
            [&rules_file](auto values) { rules_file = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While searching for rule definition:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitAnalysisFailure;
    }
    auto ruledesc_it = rules_file.find(rule_name.GetNamedTarget().name);
    if (ruledesc_it == rules_file.end()) {
        Logger::Log(LogLevel::Error,
                    "Rule definition of {} is missing",
                    rule_name.ToString());
        return kExitAnalysisFailure;
    }
    if (options.print_json) {
        PrintRuleAsOrderedJson(*ruledesc_it, rule_name.ToJson());
        return kExitSuccess;
    }
    PrettyPrintRule(
        *ruledesc_it, rule_name, repo_config, Style{options.colored}, options);
    return kExitSuccess;
}

auto DescribeTarget(BuildMaps::Target::ConfiguredTarget const& id,
                    gsl::not_null<const RepositoryConfig*> const& repo_config,
                    std::optional<ServeApi> const& serve,
                    ApiBundle const& apis,
                    std::size_t jobs,
                    DescribeOptions const& options) -> int {
    Style const style{options.colored};
    // check if target root is absent
    if (repo_config->TargetRoot(id.target.ToModule().repository)->IsAbsent()) {
        // check that we have a serve endpoint configured
        if (not serve) {
            Logger::Log(LogLevel::Error,
                        fmt::format("Root for target {} is absent but no serve "
                                    "endpoint was configured. Please provide "
                                    "--remote-serve-address and retry.",
                                    id.target.ToJson().dump()));
            return kExitBuildEnvironment;
        }
        // check that just serve and the client use same remote execution
        // endpoint; it might make sense in the future to remove or avoid this
        // check, e.g., if remote endpoints are behind proxies.
        if (not serve->CheckServeRemoteExecution()) {
            Logger::Log(LogLevel::Error,
                        "Inconsistent remote execution endpoint and serve "
                        "endpoint configuration detected.");
            return kExitBuildEnvironment;
        }
        // ask serve endpoint to provide the description
        auto const& repo_name = id.target.ToModule().repository;
        auto target_root_id =
            repo_config->TargetRoot(repo_name)->GetAbsentTreeId();
        if (not target_root_id) {
            Logger::Log(
                LogLevel::Error,
                "Failed to get the target root id for repository \"{}\"",
                repo_name);
            return kExitBuildEnvironment;
        }
        if (auto dgst = serve->ServeTargetDescription(
                *target_root_id,
                *(repo_config->TargetFileName(repo_name)),
                id.target.GetNamedTarget().name)) {
            // if we're only asked to provide rule description as JSON, as this
            // is an export target, we don't need the blob and can directly
            // provide the user the information
            if (options.print_json) {
                std::cout << nlohmann::json({{"type", "export"}}).dump(2)
                          << std::endl;
                return kExitSuccess;
            }
            auto const& desc_info =
                Artifact::ObjectInfo{.digest = *dgst, .type = ObjectType::File};
            if (not apis.local->IsAvailable(*dgst)) {
                if (not apis.remote->RetrieveToCas({desc_info}, *apis.local)) {
                    Logger::Log(LogLevel::Error,
                                "Failed to retrieve blob {} from remote CAS",
                                desc_info.ToString());
                    return kExitBuildEnvironment;
                }
            }
            auto const desc_str = apis.local->RetrieveToMemory(desc_info);
            if (not desc_str) {
                Logger::Log(LogLevel::Error,
                            "Could not load in memory blob {}",
                            desc_info.ToString());
                return kExitBuildEnvironment;
            }
            // parse blob into JSON object
            nlohmann::json desc;
            try {
                desc = nlohmann::json::parse(*desc_str);
            } catch (std::exception const& ex) {
                Logger::Log(
                    LogLevel::Error,
                    "Parsing served target description failed with:\n{}",
                    ex.what());
                return kExitBuildEnvironment;
            }
            // serve endpoint already checked that this target is of
            // "type": "export", so we can just print the description
            PrintTargetHeader(style, id.target.ToString());
            PrintRuleHeader(style, QuoteEntity("export"), /*built_in=*/true);
            PrintExportDescription(style, desc, options);
            std::cout << std::flush;
            return kExitSuccess;
        }
        // report failure to serve description
        Logger::Log(LogLevel::Error,
                    "Serve endpoint could not provide description of target {} "
                    "with absent root.",
                    id.target.ToJson().dump());
        return kExitBuildEnvironment;
    }

    // process with a present target root
    auto targets_file_map = Base::CreateTargetsFileMap(repo_config, jobs);
    nlohmann::json targets_file{};
    bool failed{false};
    {
        TaskSystem ts{jobs};
        targets_file_map.ConsumeAfterKeysReady(
            &ts,
            {id.target.ToModule()},
            [&targets_file](auto values) { targets_file = *values[0]; },
            [&failed](auto const& msg, bool fatal) {
                Logger::Log(fatal ? LogLevel::Error : LogLevel::Warning,
                            "While searching for target description:\n{}",
                            msg);
                failed = failed or fatal;
            });
    }
    if (failed) {
        return kExitAnalysisFailure;
    }
    auto desc_it = targets_file.find(id.target.GetNamedTarget().name);
    if (desc_it == targets_file.end()) {
        PrintTargetHeader(style, id.target.ToString());
        std::cout << style.Dim("        implicitly a source file") << "\n"
                  << std::flush;
        return kExitSuccess;
    }
    nlohmann::json desc = *desc_it;
    auto rule_it = desc.find("type");
    if (rule_it == desc.end()) {
        Logger::Log(LogLevel::Error,
                    "{} is a target without specified type.",
                    id.ToString());
        return kExitAnalysisFailure;
    }
    if (BuildMaps::Target::IsBuiltInRule(*rule_it)) {
        if (options.print_json) {
            // For built-in rules, we have no user-defined description to
            // provide other than informing the user that it is a built-in rule.
            std::cout << nlohmann::json({{"type", *rule_it}}).dump(2)
                      << std::endl;
            return kExitSuccess;
        }
        PrintTargetHeader(style, id.target.ToString());
        PrintRuleHeader(
            style, QuoteEntity(rule_it->get<std::string>()), /*built_in=*/true);
        if (*rule_it == "export") {
            // export targets may have doc fields of their own.
            PrintExportDescription(style, desc, options);
        }
        else if (*rule_it == "configure") {
            PrintDescription(desc);
            if (auto target = desc.find("target"); target != desc.end()) {
                PrintSection(style, "CONFIGURED TARGET");
                std::cout << kIndentEntry
                          << style.Entity(ResolveTargetName(
                                 *target, id.target, repo_config))
                          << "\n";
            }
        }
        std::cout << std::flush;
        return kExitSuccess;
    }
    auto rule_name = BuildMaps::Base::ParseEntityNameFromJson(
        *rule_it,
        id.target,
        repo_config,
        [&rule_it, &id](std::string const& parse_err) {
            Logger::Log(LogLevel::Error,
                        "Parsing rule name {} for target {} failed with:\n{}.",
                        rule_it->dump(),
                        id.ToString(),
                        parse_err);
        });
    if (not rule_name) {
        return kExitAnalysisFailure;
    }
    if (not options.print_json) {
        PrintTargetHeader(style, id.target.ToString());
    }
    return DescribeUserDefinedRule(*rule_name, repo_config, jobs, options);
}

#endif  // BOOTSTRAP_BUILD_TOOL
