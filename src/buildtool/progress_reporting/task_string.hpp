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

#ifndef INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_STRING_HPP
#define INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_STRING_HPP

#include <string>

#include "gsl/gsl"
#include "src/buildtool/progress_reporting/progress.hpp"

/// \brief Obtain the label of a task, which is the label given by the action
/// definition of the corresponding rule, or a default string, if the rule does
/// not provide one.
[[nodiscard]] auto TaskLabel(gsl::not_null<Progress*> const& progress,
                             std::string const& task) -> std::string;

/// \brief Obtain the origin of a task, which is basically the target name plus
/// action number. If the origin is unknown, the task id itself is returned.
[[nodiscard]] auto TaskOrigin(gsl::not_null<Progress*> const& progress,
                              std::string const& task) -> std::string;

#endif  // INCLUDED_SRC_BUILDTOOL_PROGRESS_REPORTING_TASK_STRING_HPP
