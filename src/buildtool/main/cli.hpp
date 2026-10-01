// Copyright 2023 Huawei Cloud Computing Technology Co., Ltd.
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

#ifndef INCLUDED_SRC_BUILDTOOL_MAIN_CLI
#define INCLUDED_SRC_BUILDTOOL_MAIN_CLI

#include <cstdint>
#include <string>

#include "gsl/gsl"
#include "src/buildtool/common/cli.hpp"
#include "src/buildtool/common/retry_cli.hpp"

/// \brief The command line of the build tool backend. It is described in a
/// namespace of its own, as the launcher uses the same names for its own
/// command line arguments.
namespace Buildtool {

/// \brief Create the subcommands of the backend, without their arguments.
void CreateSubcommands(CLI::App& app);

enum class SubCommand : std::uint8_t {
    kUnknown,
    kVersion,
    kDescribe,
    kAnalyse,
    kBuild,
    kInstall,
    kRebuild,
    kInstallCas,
    kAddToCas,
    kTraverse,
    kGc,
    kExecute,
    kServe,
    kEval
};

struct CommandLineArguments {
    CommonArguments common;
    LogArguments log;
    AnalysisArguments analysis;
    DescribeArguments describe;
    DiagnosticArguments diagnose;
    EndpointArguments endpoint;
    BuildArguments build;
    TCArguments tc;
    StageArguments stage;
    RebuildArguments rebuild;
    FetchArguments fetch;
    GraphArguments graph;
    CommonAuthArguments auth;
    ClientAuthArguments cauth;
    ServerAuthArguments sauth;
    ServiceArguments service;
    ServeArguments serve;
    EvalArguments eval;
    RetryArguments retry;
    GcArguments gc;
    ToAddArguments to_add;
    ProtocolArguments protocol;
    SubCommand cmd{SubCommand::kUnknown};
};

auto ParseCommandLineArguments(int argc,
                               char const* const* argv) -> CommandLineArguments;

/// \brief Setup the arguments of a single subcommand, for describing them in a
/// help text. Unknown subcommands are ignored.
void SetupSubcommandArguments(
    gsl::not_null<CLI::App*> const& app,
    std::string const& subcommand,
    gsl::not_null<CommandLineArguments*> const& clargs);

}  // namespace Buildtool

#endif  // INCLUDED_SRC_BUILDTOOL_MAIN_CLI
