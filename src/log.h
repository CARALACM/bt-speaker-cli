// Copyright 2026 sendspin-cpp-cli Contributors
// Modified 2026 CARALACM for bt-speaker-cli
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

/// Tagged stderr logging gated on a global log level.

#pragma once

#include <array>
#include <cstdio>
#include <string>

namespace bt_speaker_cli {

enum class LogLevel {
    NONE = 0,
    ERROR = 1,
    WARN = 2,
    INFO = 3,
    DEBUG = 4,
    VERBOSE = 5,
};

/// The tags bt-speaker-cli's log lines carry.
inline constexpr const char* LOG_TAG_CLI = "cli";
inline constexpr const char* LOG_TAG_AUDIO = "audio";
inline constexpr const char* LOG_TAG_BT = "bt";
inline constexpr const char* LOG_TAG_AGENT = "agent";
inline constexpr const char* LOG_TAG_AVRCP = "avrcp";
inline constexpr const char* LOG_TAG_BRIDGE = "bridge";
inline constexpr const char* LOG_TAG_CONTROL = "control";
inline constexpr const char* LOG_TAG_HOOK = "hook";

inline constexpr auto LOG_TAGS = std::to_array({
    LOG_TAG_CLI,
    LOG_TAG_AUDIO,
    LOG_TAG_BT,
    LOG_TAG_AGENT,
    LOG_TAG_AVRCP,
    LOG_TAG_BRIDGE,
    LOG_TAG_CONTROL,
    LOG_TAG_HOOK,
});

void set_log_level(LogLevel level);
LogLevel get_log_level();

/// Writes `<L> <tag>: <message>` to stderr if `level` passes the global level.
__attribute__((format(printf, 3, 4))) void log_line(LogLevel level, const char* tag,
                                                    const char* fmt, ...);

/// Logs a fatal startup failure at ERROR under the failing subsystem's tag, past the level gate.
__attribute__((format(printf, 2, 3))) void log_fatal(const char* tag, const char* fmt, ...);

/// Points stderr at `path`, appending, and timestamps every line. Call before -z forks.
/// @return true if `path` now carries the log; on false stderr is untouched.
bool log_to_file(const std::string& path);

/// SIGHUP handler that flags the -f logfile for reopening. Install only when -f was given.
void log_handle_sighup(int sig);

/// Reopens the -f logfile if a SIGHUP asked for it. Call from the main loop.
void log_reopen_if_requested();

}  // namespace bt_speaker_cli

/// Logs under the calling file's `static constexpr const char* LOG_TAG`.
#define cli_log(level, ...) ::bt_speaker_cli::log_line((level), LOG_TAG, __VA_ARGS__)
