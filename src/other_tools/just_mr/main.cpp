// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
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
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "CLI/CLI.hpp"
#include "gsl/gsl"
#include "nlohmann/json.hpp"
#include "src/buildtool/common/clidefaults.hpp"
#include "src/buildtool/common/retry_cli.hpp"
#include "src/buildtool/common/user_structs.hpp"
#include "src/buildtool/crypto/hash_function.hpp"
#include "src/buildtool/file_system/file_system_manager.hpp"
#include "src/buildtool/file_system/git_context.hpp"
#include "src/buildtool/logging/log_config.hpp"
#include "src/buildtool/logging/log_level.hpp"
#include "src/buildtool/logging/log_sink_cmdline.hpp"
#include "src/buildtool/logging/log_sink_file.hpp"
#include "src/buildtool/logging/logger.hpp"
#include "src/buildtool/main/cli.hpp"
#include "src/buildtool/main/version.hpp"
#include "src/buildtool/storage/config.hpp"
#include "src/buildtool/storage/garbage_collector.hpp"
#include "src/buildtool/storage/repository_garbage_collector.hpp"
#include "src/buildtool/storage/storage.hpp"
#include "src/buildtool/system/terminal.hpp"
#include "src/other_tools/just_mr/cli.hpp"
#include "src/other_tools/just_mr/exit_codes.hpp"
#include "src/other_tools/just_mr/fetch.hpp"
#include "src/other_tools/just_mr/launch.hpp"
#include "src/other_tools/just_mr/mirrors.hpp"
#include "src/other_tools/just_mr/rc.hpp"
#include "src/other_tools/just_mr/setup.hpp"
#include "src/other_tools/just_mr/setup_utils.hpp"
#include "src/other_tools/just_mr/update.hpp"
#include "src/other_tools/just_mr/utils.hpp"
#include "src/utils/cpp/expected.hpp"

namespace {

namespace Backend = Buildtool;

/// \brief Groups of options of jst itself, selected per subcommand.
enum class OptionGroup : std::uint8_t {
    kRc,
    kLog,
    kBackend,
    kConfig,
    kBuildRoot,
    kLauncher,
    kDefines,
    kRemote,
    kServe
};

/// \brief Setup a single group of options of jst itself.
void SetupOptionGroup(OptionGroup group,
                      gsl::not_null<CLI::App*> const& app,
                      gsl::not_null<CommandLineArguments*> const& clargs) {
    switch (group) {
        case OptionGroup::kRc:
            SetupMultiRepoRcArguments(app, &clargs->common);
            break;
        case OptionGroup::kLog:
            SetupMultiRepoLogArguments(app, &clargs->log);
            break;
        case OptionGroup::kBackend:
            SetupMultiRepoBackendArguments(app, &clargs->common);
            break;
        case OptionGroup::kConfig:
            SetupMultiRepoConfigArguments(app, &clargs->common);
            break;
        case OptionGroup::kBuildRoot:
            SetupMultiRepoBuildRootArguments(app, &clargs->common);
            break;
        case OptionGroup::kLauncher:
            SetupMultiRepoLauncherArguments(app, &clargs->common);
            break;
        case OptionGroup::kDefines:
            SetupMultiRepoDefinesArguments(app, &clargs->common);
            break;
        case OptionGroup::kRemote:
            SetupMultiRepoRemoteArguments(app, &clargs->common);
            SetupMultiRepoRemoteAuthArguments(app, &clargs->auth);
            SetupRetryArguments(app, &clargs->retry);
            break;
        case OptionGroup::kServe:
            SetupMultiRepoServeArguments(app, &clargs->common);
            break;
    }
}

/// \brief Setup the options of jst itself for a subcommand, steered by flags.
void SetupJstArguments(gsl::not_null<CLI::App*> const& app,
                       JustSubCmdFlags const& flags,
                       gsl::not_null<CommandLineArguments*> const& clargs,
                       bool launches_backend = false) {
    SetupOptionGroup(OptionGroup::kRc, app, clargs);
    SetupOptionGroup(OptionGroup::kLog, app, clargs);
    // the setup of computed roots launches the backend itself
    if (flags.config or launches_backend) {
        SetupOptionGroup(OptionGroup::kBackend, app, clargs);
    }
    if (flags.config) {
        SetupOptionGroup(OptionGroup::kConfig, app, clargs);
    }
    if (flags.build_root) {
        SetupOptionGroup(OptionGroup::kBuildRoot, app, clargs);
    }
    // the setup runs actions locally itself, using the same launcher
    if (flags.config or flags.launch) {
        SetupOptionGroup(OptionGroup::kLauncher, app, clargs);
    }
    if (flags.defines) {
        SetupOptionGroup(OptionGroup::kDefines, app, clargs);
    }
    if (flags.remote) {
        SetupOptionGroup(OptionGroup::kRemote, app, clargs);
    }
    if (flags.serve) {
        SetupOptionGroup(OptionGroup::kServe, app, clargs);
    }
}

/// \brief The canonical name of an option, as used in diagnostics.
[[nodiscard]] auto OptionName(CLI::Option const& option) -> std::string {
    if (not option.get_lnames().empty()) {
        return "--" + option.get_lnames().front();
    }
    if (not option.get_snames().empty()) {
        return "-" + option.get_snames().front();
    }
    return option.get_name();
}

// The flags of jst's own subcommands; those describing what is forwarded to
// the backend are irrelevant here, as these subcommands do not launch it.
constexpr JustSubCmdFlags kSetupFlags{.config = true,
                                      .parallel = true,
                                      .build_root = true,
                                      .launch = true,
                                      .defines = false,
                                      .remote = true,
                                      .remote_props = false,
                                      .serve = true,
                                      .dispatch = false,
                                      .does_build = false};
constexpr JustSubCmdFlags kUpdateFlags{.config = true,
                                       .parallel = true,
                                       .build_root = true,
                                       .launch = true,
                                       .defines = false,
                                       .remote = false,
                                       .remote_props = false,
                                       .serve = false,
                                       .dispatch = false,
                                       .does_build = false};
constexpr JustSubCmdFlags kGcRepoFlags{.config = false,
                                       .parallel = false,
                                       .build_root = true,
                                       .launch = false,
                                       .defines = false,
                                       .remote = false,
                                       .remote_props = false,
                                       .serve = false,
                                       .dispatch = false,
                                       .does_build = false};
// The backend passthrough hands over to the backend directly, so only the
// options of jst concerning that call itself apply to it.
constexpr JustSubCmdFlags kBackendFlags{.config = false,
                                        .parallel = false,
                                        .build_root = false,
                                        .launch = false,
                                        .defines = false,
                                        .remote = false,
                                        .remote_props = false,
                                        .serve = false,
                                        .dispatch = false,
                                        .does_build = false};
// All root-level options of jst itself, as accepted before the subcommand
constexpr JustSubCmdFlags kRootFlags{.config = true,
                                     .parallel = true,
                                     .build_root = true,
                                     .launch = true,
                                     .defines = true,
                                     .remote = true,
                                     .remote_props = true,
                                     .serve = true,
                                     .dispatch = true,
                                     .does_build = true};

/// \brief Setup arguments for subcommand "just-mr fetch".
void SetupFetchCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoSetupArguments(app, &clargs->setup);
    SetupMultiRepoFetchArguments(app, &clargs->fetch);
}

/// \brief Setup arguments for subcommand "just-mr update".
void SetupUpdateCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoUpdateArguments(app, &clargs->update);
}

/// \brief Setup arguments for subcommand "just-mr gc-repo".
void SetupGcRepoCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoGcArguments(app, &clargs->gc);
}

/// \brief Setup arguments for subcommand "just-mr setup" and
/// "just-mr setup-env".
void SetupSetupCommandArguments(
    gsl::not_null<CLI::App*> const& app,
    gsl::not_null<CommandLineArguments*> const& clargs) {
    SetupMultiRepoSetupArguments(app, &clargs->setup);
}

/// \brief Print the help text of a subcommand launching the backend, listing
/// the options of jst itself as well as those the backend accepts for it.
void PrintSubcommandHelp(std::string const& name,
                         gsl::not_null<CLI::App const*> const& backend_subcmd) {
    CLI::App help_app{backend_subcmd->get_description(), "jst " + name};
    help_app.option_defaults()->take_last();
    // the backend's options first, as they define the positional arguments
    Backend::CommandLineArguments backend_clargs{};
    Backend::SetupSubcommandArguments(&help_app, name, &backend_clargs);
    // those of jst replace the ones it consumes itself, as it is jst that acts
    // on them; the remaining ones are added to the end of the list
    CommandLineArguments clargs{};
    CLI::App jst_app{};
    SetupJstArguments(&jst_app, kKnownJustSubcommands.at(name), &clargs, true);
    for (auto const* option : jst_app.get_options()) {
        if (option == jst_app.get_help_ptr()) {
            continue;
        }
        if (auto* known = help_app.get_option_no_throw(OptionName(*option))) {
            help_app.remove_option(known);
        }
    }
    SetupJstArguments(&help_app, kKnownJustSubcommands.at(name), &clargs, true);
    std::cout << help_app.help() << std::endl;
}

/// \brief The recorded arguments, segregated by options and positionals.
struct RecordedArgs {
    std::vector<std::string> options;
    std::vector<std::string> positionals;
};

/// \brief Record the arguments of the options of the backend, to forward them
/// once it is launched. Options are recorded as "--name=value", so that a value
/// can never be taken for an option; positional arguments are kept apart, as
/// they are forwarded behind a "--" for the very same reason.
void RecordBackendArgs(gsl::not_null<CLI::App*> const& app,
                       gsl::not_null<RecordedArgs*> const& args) {
    for (auto* option : app->get_options()) {
        if (option->get_positional()) {
            option->each([args](std::string const& value) {
                args->positionals.emplace_back(value);
            });
        }
        else {
            option->each(
                [args, name = OptionName(*option)](std::string const& value) {
                    auto arg = name;
                    arg += "=";
                    arg += value;
                    args->options.emplace_back(std::move(arg));
                });
        }
        // record in the order given on the command line, not per option
        option->trigger_on_parse();
    }
}

/// \brief Reconstruct the arguments to forward to the backend: its options,
/// followed by its positional arguments behind a "--", so that none of the
/// latter can be taken for an option. The extras are the arguments neither of
/// the two knows; they are forwarded as well, for the backend to report them.
[[nodiscard]] auto ReconstructBackendArgV(
    RecordedArgs const& args,
    std::vector<std::string> const& extras) -> std::vector<std::string> {
    auto argv = args.options;
    for (auto const& extra : extras) {
        if (extra != "--") {  // a "--" given is dropped, one is added below
            argv.emplace_back(extra);
        }
    }
    if (not args.positionals.empty()) {
        argv.emplace_back("--");
        argv.insert(
            argv.end(), args.positionals.begin(), args.positionals.end());
    }
    return argv;
}

/// \brief Setup the arguments of the backend for a subcommand launching it, so
/// that the whole command line is understood, and record the ones only the
/// backend knows, to forward them to it later.
void SetupBackendOnlySubcommandArguments(
    gsl::not_null<CLI::App*> const& app,
    JustSubCmdFlags const& flags,
    gsl::not_null<Backend::CommandLineArguments*> const& backend_clargs,
    gsl::not_null<RecordedArgs*> const& backend_args) {
    // the backend's arguments first, as they define the positional ones
    Backend::SetupSubcommandArguments(app, app->get_name(), backend_clargs);
    // for the options both define, the definition of jst is used, as it is jst
    // that acts on them and forwards them itself; so drop them here
    CLI::App jst_app{};
    CommandLineArguments clargs{};
    SetupJstArguments(&jst_app, flags, &clargs, /*launches_backend=*/true);
    for (auto const* option : jst_app.get_options()) {
        if (option == jst_app.get_help_ptr()) {
            continue;
        }
        if (auto* known = app->get_option_no_throw(OptionName(*option))) {
            app->remove_option(known);
        }
    }
    // what remains are the arguments only the backend knows
    RecordBackendArgs(app, backend_args);
}

[[nodiscard]] auto ParseCommandLineArguments(int argc, char const* const* argv)
    -> CommandLineArguments {
    CLI::App app(
        "jst, a multi-repository configuration tool and launcher for the "
        "build tool",
        "jst");
    app.option_defaults()->take_last();
    auto* cmd_mrversion = app.add_subcommand(
        "version", "Print version information in JSON format of this tool.");
    auto* cmd_setup = app.add_subcommand(
        "setup", "Setup and generate configuration for the build tool");
    auto* cmd_setup_env = app.add_subcommand(
        "setup-env", "Setup without workspace root for the main repository.");
    auto* cmd_fetch =
        app.add_subcommand("fetch", "Fetch and store distribution files.");
    auto* cmd_update = app.add_subcommand(
        "update",
        "Advance Git commit IDs and print updated jst configuration.");
    auto* cmd_backend = app.add_subcommand(
        "backend", "Canonical way to call the build backend directly.");
    auto* cmd_gc_repo = app.add_subcommand(
        "gc-repo", "Perform garbage collection on the repository roots.");
    cmd_backend->set_help_flag();  // disable help flag
    // define just subcommands
    CLI::App app_backend_subcommands("jst_backend subcommands.");
    Backend::CreateSubcommands(app_backend_subcommands);
    std::vector<CLI::App*> cmd_just_subcmds{};
    cmd_just_subcmds.reserve(kKnownJustSubcommands.size());
    for (auto const& known_subcmd : kKnownJustSubcommands) {
        auto* subcmd = app.add_subcommand(
            known_subcmd.first,
            app_backend_subcommands.get_subcommand(known_subcmd.first)
                ->get_description());
        subcmd->set_help_flag();  // disable help flag
        cmd_just_subcmds.emplace_back(subcmd);
    }
    app.require_subcommand(1);

    CommandLineArguments clargs;
    // allow common jst arguments in two places:
    // 1. before the subcommand, at the root level
    SetupJstArguments(&app, kRootFlags, &clargs, /*launches_backend=*/true);
    // 2. after the subcommand, but only a meaningful subset; "version" takes
    //    none, as it only prints the version information of jst itself
    SetupJstArguments(cmd_setup, kSetupFlags, &clargs);
    SetupJstArguments(cmd_setup_env, kSetupFlags, &clargs);
    SetupJstArguments(cmd_fetch, kSetupFlags, &clargs);
    SetupJstArguments(cmd_update, kUpdateFlags, &clargs);
    SetupJstArguments(cmd_gc_repo, kGcRepoFlags, &clargs);
    SetupJstArguments(
        cmd_backend, kBackendFlags, &clargs, /*launches_backend=*/true);

    // setup the normal jst subcommand arguments
    SetupSetupCommandArguments(cmd_setup, &clargs);
    SetupSetupCommandArguments(cmd_setup_env, &clargs);
    SetupFetchCommandArguments(cmd_fetch, &clargs);
    SetupUpdateCommandArguments(cmd_update, &clargs);
    SetupGcRepoCommandArguments(cmd_gc_repo, &clargs);

    // setup the backend subcommands so that all arguments are understood
    Backend::CommandLineArguments backend_clargs{};
    RecordedArgs recorded_args{};
    for (auto* sub_cmd : cmd_just_subcmds) {
        auto const& flags = kKnownJustSubcommands.at(sub_cmd->get_name());
        // setup backend-only arguments for subcommand, without those arguments
        // that are also supported by jst (filtered out to avoid collision);
        // the ones only the backend knows are recorded while parsing
        SetupBackendOnlySubcommandArguments(
            sub_cmd, flags, &backend_clargs, &recorded_args);
        // setup common jst arguments, only the relevant subset steered by flags
        SetupJstArguments(sub_cmd, flags, &clargs, /*launches_backend=*/true);
    }

    // for 'just' calls, allow extra arguments
    cmd_backend->allow_extras();
    // stop parsing at the backend subcommand, so that everything from there on
    // is forwarded unchanged
    cmd_backend->prefix_command();
    for (auto const& sub_cmd : cmd_just_subcmds) {
        sub_cmd->allow_extras();
    }

    try {
        app.parse(argc, argv);
    } catch (CLI::Error& e) {
        // CLI11 throws for things like --help calls for them to be handled
        // separately by parse callers. In this case it nevertheless sets the
        // error code to 0 (success).
        auto const err = app.exit(e);
        std::exit(err == 0 ? kExitSuccess : kExitClargsError);
    } catch (std::exception const& ex) {
        Logger::Log(LogLevel::Error, "Command line parse error: {}", ex.what());
        std::exit(kExitClargsError);
    }

    if (*cmd_mrversion) {
        clargs.cmd = SubCommand::kMRVersion;
    }
    else if (*cmd_setup) {
        clargs.cmd = SubCommand::kSetup;
    }
    else if (*cmd_setup_env) {
        clargs.cmd = SubCommand::kSetupEnv;
    }
    else if (*cmd_fetch) {
        clargs.cmd = SubCommand::kFetch;
    }
    else if (*cmd_update) {
        clargs.cmd = SubCommand::kUpdate;
    }
    else if (*cmd_gc_repo) {
        clargs.cmd = SubCommand::kGcRepo;
    }
    else if (*cmd_backend) {
        clargs.cmd = SubCommand::kJustBackend;
        // get remaining args
        clargs.just_cmd.additional_just_args = cmd_backend->remaining();
    }
    else {
        for (auto const& sub_cmd : cmd_just_subcmds) {
            if (*sub_cmd) {
                clargs.cmd = SubCommand::kJustSubCmd;
                clargs.just_cmd.subcmd_name =
                    sub_cmd->get_name();  // get name of subcommand
                clargs.just_cmd.additional_just_args =
                    ReconstructBackendArgV(recorded_args, sub_cmd->remaining());
                break;  // no need to go further
            }
        }
    }

    // for the subcommands launching the backend, describe its options together
    // with the ones of jst, instead of launching it to print its own help
    if (clargs.cmd == SubCommand::kJustSubCmd) {
        auto const& args = clargs.just_cmd.additional_just_args;
        if (std::any_of(args.begin(), args.end(), [](auto const& arg) {
                return arg == "-h" or arg == "--help";
            })) {
            auto const& name = *clargs.just_cmd.subcmd_name;
            PrintSubcommandHelp(name,
                                app_backend_subcommands.get_subcommand(name));
            std::exit(kExitSuccess);
        }
    }

    return clargs;
}

void SetupDefaultLogging() {
    LogConfig::SetLogLimit(kDefaultLogLevel);
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(Terminal::UseColor())});
}

void SetupLogging(MultiRepoLogArguments const& clargs) {
    if (clargs.log_limit) {
        LogConfig::SetLogLimit(*clargs.log_limit);
    }
    else {
        LogConfig::SetLogLimit(kDefaultLogLevel);
    }
    LogConfig::SetSinks({LogSinkCmdLine::CreateFactory(
        clargs.color.value_or(Terminal::UseColor()),
        clargs.restrict_stderr_log_limit)});
    for (auto const& log_file : clargs.log_files) {
        LogConfig::AddSink(LogSinkFile::CreateFactory(
            log_file,
            clargs.log_append ? LogSinkFile::Mode::Append
                              : LogSinkFile::Mode::Overwrite));
    }
}

[[nodiscard]] auto CreateStorageConfig(MultiRepoCommonArguments const& args,
                                       HashFunction::Type hash_type) noexcept
    -> std::optional<StorageConfig> {
    StorageConfig::Builder builder;
    if (args.just_mr_paths->root.has_value()) {
        builder.SetBuildRoot(*args.just_mr_paths->root);
    }
    builder.SetHashType(hash_type);

    // As just-mr does not require the TargetCache, we do not need to set any of
    // the remote execution fields for the backend description.
    auto config = builder.Build();
    if (config) {
        return *std::move(config);
    }
    Logger::Log(LogLevel::Error, config.error());
    return std::nullopt;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    SetupDefaultLogging();
    std::string my_name{};
    if (argc > 0) {
        try {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            my_name = std::filesystem::path(argv[0]).filename().string();
        } catch (...) {
            // ignore, as my_name is only used for error messages
            my_name.clear();
        }
    }
    try {
        // get the user-defined arguments
        auto arguments = ParseCommandLineArguments(argc, argv);

        if (arguments.cmd == SubCommand::kMRVersion) {
            std::cout << version() << std::endl;
            return kExitSuccess;
        }

        SetupLogging(arguments.log);
        if (arguments.common.just_mr_paths->workspace_root) {
            Logger::Log(
                LogLevel::Verbose,
                "Using setup root {}",
                arguments.common.just_mr_paths->workspace_root->string());
        }
        // Parse rc file, if given, and returns any configuration file found in
        // known locations, with the setup root updated accordingly
        auto config_file = ReadJustMRRC(&arguments);
        // As the rc file can contain logging parameters, reset the logging
        // configuration
        SetupLogging(arguments.log);
        // An explicitly given configuration file path wins. In this case, the
        // default setup root must be used
        if (arguments.common.repository_config) {
            config_file = arguments.common.repository_config;
            arguments.common.just_mr_paths->setup_root = kDefaultSetupRoot;
        }

        // if optional args were not read from the jstrc or given by user, use
        // the defaults
        if (not arguments.common.just_path) {
            arguments.common.just_path = kDefaultBackendPath;
        }
        if (not arguments.common.git_path) {
            arguments.common.git_path = kDefaultGitPath;
        }
        bool forward_build_root = true;
        if (not arguments.common.just_mr_paths->root) {
            forward_build_root = false;
            arguments.common.just_mr_paths->root =
                std::filesystem::weakly_canonical(kDefaultBuildRoot);
        }
        if (not arguments.common.checkout_locations_file and
            FileSystemManager::IsFile(std::filesystem::weakly_canonical(
                kDefaultCheckoutLocationsFile))) {
            arguments.common.checkout_locations_file =
                std::filesystem::weakly_canonical(
                    kDefaultCheckoutLocationsFile);
        }
        if (arguments.common.just_mr_paths->distdirs.empty()) {
            arguments.common.just_mr_paths->distdirs.emplace_back(
                kDefaultDistdirs);
        }

        // read checkout locations and alternative mirrors
        if (arguments.common.checkout_locations_file) {
            try {
                std::ifstream ifs(*arguments.common.checkout_locations_file);
                auto checkout_locations_json = nlohmann::json::parse(ifs);
                arguments.common.just_mr_paths->git_checkout_locations =
                    checkout_locations_json
                        .value("checkouts", nlohmann::json::object())
                        .value("git", nlohmann::json::object());
                arguments.common.alternative_mirrors->local_mirrors =
                    checkout_locations_json.value("local mirrors",
                                                  nlohmann::json::object());
                arguments.common.alternative_mirrors->preferred_hostnames =
                    checkout_locations_json.value("preferred hostnames",
                                                  nlohmann::json::array());
                arguments.common.alternative_mirrors->extra_inherit_env =
                    checkout_locations_json.value("extra inherit env",
                                                  nlohmann::json::array());
            } catch (std::exception const& e) {
                Logger::Log(
                    LogLevel::Error,
                    "Parsing checkout locations file {} failed with error:\n{}",
                    arguments.common.checkout_locations_file->string(),
                    e.what());
                std::exit(kExitConfigError);
            }
        }

        // append explicitly-given distdirs
        arguments.common.just_mr_paths->distdirs.insert(
            arguments.common.just_mr_paths->distdirs.end(),
            arguments.common.explicit_distdirs.begin(),
            arguments.common.explicit_distdirs.end());

        // "backend" hands over to the build tool backend directly, without
        // any of the operations jst performs itself
        if (arguments.cmd == SubCommand::kJustBackend) {
            return CallBackend(arguments.common, arguments.just_cmd);
        }

        // Setup LocalStorageConfig to store the local_build_root properly
        // and make the cas and git cache roots available. A native storage is
        // always instantiated, while a compatible one only if needed.
        auto const native_storage_config =
            CreateStorageConfig(arguments.common, HashFunction::Type::GitSHA1);
        if (not native_storage_config) {
            Logger::Log(LogLevel::Error,
                        "Failed to configure local build root.");
            return kExitGenericFailure;
        }

        if (arguments.cmd == SubCommand::kGcRepo) {
            return RepositoryGarbageCollector::TriggerGarbageCollection(
                       *native_storage_config, arguments.gc.drop_only)
                       ? kExitSuccess
                       : kExitBuiltinCommandFailure;
        }

        auto const native_storage = Storage::Create(&*native_storage_config);

        // check for conflicts in main repo name
        if ((not arguments.setup.sub_all) and arguments.common.main and
            arguments.setup.sub_main and
            (arguments.common.main != arguments.setup.sub_main)) {
            Logger::Log(LogLevel::Warning,
                        "Conflicting options for main repository, selecting {}",
                        *arguments.setup.sub_main);
        }
        if (arguments.setup.sub_main) {
            arguments.common.main = arguments.setup.sub_main;
        }

        // check for errors in setting up local launcher arg
        if (not arguments.common.local_launcher) {
            Logger::Log(LogLevel::Error,
                        "Failed to configure local execution.");
            return kExitGenericFailure;
        }

        /**
         * The current implementation of libgit2 uses pthread_key_t incorrectly
         * on POSIX systems to handle thread-specific data, which requires us to
         * explicitly make sure the main thread is the first one to call
         * git_libgit2_init. Future versions of libgit2 will hopefully fix this.
         */
        GitContext::Create();

        // Run subcommands known to the backend, with the setup jst performs
        // for them
        if (arguments.cmd == SubCommand::kJustSubCmd) {
            return CallJust(config_file,
                            arguments.invocation_log,
                            arguments.common,
                            arguments.setup,
                            arguments.just_cmd,
                            arguments.log,
                            arguments.auth,
                            arguments.retry,
                            arguments.launch_fwd,
                            *native_storage_config,
                            native_storage,
                            forward_build_root,
                            my_name);
        }
        auto repo_lock =
            RepositoryGarbageCollector::SharedLock(*native_storage_config);
        if (not repo_lock) {
            return kExitGenericFailure;
        }
        auto lock = GarbageCollector::SharedLock(*native_storage_config);
        if (not lock) {
            return kExitGenericFailure;
        }

        // The remaining options all need the config file
        auto config = JustMR::Utils::ReadConfiguration(
            config_file, arguments.common.absent_repository_file);
        if (not config) {
            Logger::Log(LogLevel::Error,
                        "Cannot find repository configuration.");
        }

        // Run subcommand `setup` or `setup-env`
        if (arguments.cmd == SubCommand::kSetup or
            arguments.cmd == SubCommand::kSetupEnv) {
            auto mr_config_path = MultiRepoSetup(
                config,
                arguments.common,
                arguments.log,
                arguments.setup,
                arguments.just_cmd,
                arguments.auth,
                arguments.retry,
                *native_storage_config,
                native_storage,
                /*interactive=*/(arguments.cmd == SubCommand::kSetupEnv),
                my_name);
            // dump resulting config to stdout
            if (not mr_config_path) {
                return kExitSetupError;
            }
            // report success
            Logger::Log(LogLevel::Info, "Setup completed");
            // print config file to stdout
            std::cout << mr_config_path->first.string() << std::endl;
            return kExitSuccess;
        }

        // Run subcommand `update`
        if (arguments.cmd == SubCommand::kUpdate) {
            return MultiRepoUpdate(config,
                                   arguments.common,
                                   arguments.update,
                                   *native_storage_config,
                                   my_name);
        }

        // Run subcommand `fetch`
        if (arguments.cmd == SubCommand::kFetch) {
            return MultiRepoFetch(config,
                                  arguments.common,
                                  arguments.setup,
                                  arguments.fetch,
                                  arguments.auth,
                                  arguments.retry,
                                  *native_storage_config,
                                  native_storage,
                                  my_name);
        }

        // Unknown subcommand should fail
        Logger::Log(LogLevel::Error, "Unknown subcommand provided.");
        return kExitUnknownCommand;
    } catch (std::exception const& ex) {
        Logger::Log(
            LogLevel::Error, "Caught exception with message: {}", ex.what());
    }
    return kExitGenericFailure;
}
