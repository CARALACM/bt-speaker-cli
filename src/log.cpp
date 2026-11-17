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

#include "log.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace bt_speaker_cli {

static constexpr const char* LOG_TAG = LOG_TAG_CLI;

namespace {

std::atomic<int> g_log_level{static_cast<int>(LogLevel::INFO)};
std::string g_logfile;
volatile sig_atomic_t g_reopen_requested = 0;

constexpr size_t INLINE_MESSAGE_BYTES = 512;

char level_letter(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR:
            return 'E';
        case LogLevel::WARN:
            return 'W';
        case LogLevel::INFO:
            return 'I';
        case LogLevel::DEBUG:
            return 'D';
        case LogLevel::VERBOSE:
            return 'V';
        case LogLevel::NONE:
            break;
    }
    return '?';
}

void write_timestamp(char* out, size_t size) {
    out[0] = '\0';
    if (g_logfile.empty()) {
        return;
    }
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    if (::gmtime_r(&now, &utc) == nullptr) {
        return;
    }
    if (std::strftime(out, size, "%Y-%m-%dT%H:%M:%SZ ", &utc) == 0) {
        out[0] = '\0';
    }
}

__attribute__((format(printf, 3, 0))) void emit(LogLevel level, const char* tag, const char* fmt,
                                                va_list args) {
    char inline_message[INLINE_MESSAGE_BYTES];
    std::vector<char> long_message;
    const char* message = inline_message;

    va_list retry;
    va_copy(retry, args);
    const int needed = std::vsnprintf(inline_message, sizeof(inline_message), fmt, args);
    if (needed < 0) {
        message = "(this log line could not be formatted)";
    } else if (static_cast<size_t>(needed) >= sizeof(inline_message)) {
        long_message.resize(static_cast<size_t>(needed) + 1);
        std::vsnprintf(long_message.data(), long_message.size(), fmt, retry);
        message = long_message.data();
    }
    va_end(retry);

    char stamp[32];
    write_timestamp(stamp, sizeof(stamp));
    std::fprintf(stderr, "%s%c %s: %s\n", stamp, level_letter(level), tag, message);
    std::fflush(stderr);
}

bool point_stderr_at(const std::string& path) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    std::fflush(stderr);
    const bool ok = ::dup2(fd, STDERR_FILENO) >= 0;
    const int reason = errno;
    ::close(fd);
    if (!ok) {
        errno = reason;
    }
    return ok;
}

}  // namespace

void set_log_level(LogLevel level) {
    g_log_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel get_log_level() {
    return static_cast<LogLevel>(g_log_level.load(std::memory_order_relaxed));
}

void log_line(LogLevel level, const char* tag, const char* fmt, ...) {
    if (get_log_level() < level) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    emit(level, tag, fmt, args);
    va_end(args);
}

void log_fatal(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    emit(LogLevel::ERROR, tag, fmt, args);
    va_end(args);
}

bool log_to_file(const std::string& path) {
    if (!point_stderr_at(path)) {
        std::fprintf(stderr, "error: cannot open logfile %s: %s\n", path.c_str(),
                     std::strerror(errno));
        return false;
    }
    g_logfile = path;
    return true;
}

void log_handle_sighup(int /*sig*/) {
    g_reopen_requested = 1;
}

void log_reopen_if_requested() {
    if (g_reopen_requested == 0) {
        return;
    }
    g_reopen_requested = 0;
    if (g_logfile.empty()) {
        return;
    }

    if (point_stderr_at(g_logfile)) {
        cli_log(LogLevel::INFO, "Reopened %s on SIGHUP", g_logfile.c_str());
        return;
    }
    log_fatal(LOG_TAG, "cannot reopen %s on SIGHUP, still logging to the previous file: %s",
              g_logfile.c_str(), std::strerror(errno));
}

}  // namespace bt_speaker_cli
