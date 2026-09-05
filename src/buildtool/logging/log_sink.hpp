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

#ifndef INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP
#define INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP

#include <cstdint>
#include <functional>
#include <istream>
#include <memory>
#include <string>

#include "src/buildtool/logging/log_level.hpp"

// forward declaration
class Logger;

/// \brief The style a log message is emitted in.
enum class MessageStyle : std::uint8_t {
    // Message is emitted with the usual log level prefix
    Normal,
    // Message is emitted without prefix and cleared by the next log message
    Volatile
};

/// \brief A message that renders itself with or without styling, e.g.,
/// coloring, depending on what the sink emitting it supports.
using StyledMessage = std::function<std::string(bool colored)>;

class ILogSink {
  public:
    using Ptr = std::shared_ptr<ILogSink>;
    ILogSink() noexcept = default;
    ILogSink(ILogSink const&) = delete;
    ILogSink(ILogSink&&) = delete;
    auto operator=(ILogSink const&) -> ILogSink& = delete;
    auto operator=(ILogSink&&) -> ILogSink& = delete;
    virtual ~ILogSink() noexcept = default;

    /// \brief Thread-safe emitting of log messages.
    /// Logger might be 'nullptr' if called from the global context.
    /// \param style    Style the message is emitted in. Not all sinks support
    ///                 all styles.
    virtual void Emit(Logger const* logger,
                      LogLevel level,
                      std::string const& msg,
                      MessageStyle style) const noexcept = 0;

    /// \brief Thread-safe emitting of log messages that can be styled.
    /// Sinks that support styling are expected to override this method; by
    /// default, the message is rendered without any styling.
    virtual void Emit(Logger const* logger,
                      LogLevel level,
                      StyledMessage const& msg,
                      MessageStyle style) const noexcept {
        Emit(logger, level, msg(/*colored=*/false), style);
    }

  protected:
    /// \brief Helper class for line iteration with std::istream_iterator.
    class Line : public std::string {
        friend auto operator>>(std::istream& is, Line& line) -> std::istream& {
            return std::getline(is, line);
        }
    };
};

using LogSinkFactory = std::function<ILogSink::Ptr()>;

#endif  // INCLUDED_SRC_BUILDTOOL_LOGGING_LOG_SINK_HPP
