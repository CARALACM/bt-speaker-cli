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

/// Command line surface for bt-speaker-cli.

#pragma once

#include "log.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bt_speaker_cli {

#ifdef BT_SPEAKER_CLI_HAVE_PULSE
inline constexpr const char* DEFAULT_OUTPUT_DEVICE = "pulse";
#elif defined(BT_SPEAKER_CLI_HAVE_PIPEWIRE)
inline constexpr const char* DEFAULT_OUTPUT_DEVICE = "pipewire";
#elif defined(BT_SPEAKER_CLI_HAVE_ALSA)
inline constexpr const char* DEFAULT_OUTPUT_DEVICE = "default";
#else
inline constexpr const char* DEFAULT_OUTPUT_DEVICE = "null";
#endif

inline constexpr uint32_t DEFAULT_BUFFER_MS = 100;
inline constexpr uint32_t MIN_BUFFER_MS = 10;
inline constexpr uint32_t MAX_BUFFER_MS = 2000;

enum class Opt : unsigned {
    Device,
    ListDevices,
    Name,
    Adapter,
    TargetDevice,
    AutoConnect,
    Discoverable,
    AllowUnpaired,
    RequireConfirm,
    NoDbus,
    Daemonize,
    Pidfile,
    Logfile,
    LogLevel,
    BufferMs,
    OutputDelay,
    ControlSocket,
    NoControl,
    StateDir,
    Config,
    HookStart,
    HookStop,
    FixedVolume,
    AlsaMixer,
    AlsaMixerCurve,
    ReleaseDevice,
};

struct Options {
    std::string device{DEFAULT_OUTPUT_DEVICE};
    bool list_devices{false};
    std::string name;
    std::string adapter;
    std::string target_device;
    bool auto_connect{true};
    bool discoverable{true};
    bool allow_unpaired{true};
    bool no_dbus{false};

    bool daemonize{false};
    std::string pidfile;
    std::string logfile;
    LogLevel log_level{LogLevel::INFO};

    uint32_t buffer_ms{DEFAULT_BUFFER_MS};
    uint16_t output_delay_ms{0};
    bool fixed_volume{false};
    std::string alsa_mixer;
    std::string alsa_mixer_curve{"spec"};
    bool release_device{false};

    bool no_control{false};
    std::string hook_start;
    std::string hook_stop;
    std::string state_dir;
    std::string config_path;

    bool show_help{false};
    bool show_version{false};

    std::string control_socket;
    std::string control_absent_reason;

    std::string subcommand;
    std::vector<std::string> subcommand_args;

    bool was_given(Opt opt) const {
        return (this->given_ & Options::bit(opt)) != 0;
    }

    void mark_given(Opt opt) {
        this->given_ |= Options::bit(opt);
    }

private:
    static constexpr uint32_t bit(Opt opt) {
        return 1U << static_cast<unsigned>(opt);
    }

    uint32_t given_{0};
};

bool parse_options(int argc, char* argv[], Options& out, std::FILE* err = stderr);
void print_usage(std::FILE* out, const char* prog);
void print_version(std::FILE* out);
std::string default_speaker_name();

}  // namespace bt_speaker_cli
