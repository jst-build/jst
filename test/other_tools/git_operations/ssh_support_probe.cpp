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

#include <cstdio>

#include "src/buildtool/file_system/git_context.hpp"
#include "src/other_tools/git_operations/git_repo_remote.hpp"

/// \brief Reports whether the linked libgit2 handles SSH remotes natively, by
/// executing the system's ssh binary. Takes no arguments.
/// Prints "native" or "shell-out" and exits with 0 or 1, respectively, such
/// that tests can be conditioned on the SSH capabilities of the build at hand.
auto main() -> int {
    GitContext::Create();
    bool const native = GitRepoRemote::HasNativeSshSupport();
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    std::printf("%s\n", native ? "native" : "shell-out");
    return native ? 0 : 1;
}
