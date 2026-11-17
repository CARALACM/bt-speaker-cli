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

#include "cli.h"

#include "audio_sink.h"
#include "bt_reconnect.h"
#include "config_file.h"
#include "control.h"
#include "log.h"

#include <getopt.h>
#include <limits.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace bt_speaker_cli {

namespace {

constexpr const char* FALLBACK_NAME = "bt-speaker";

enum LongOnly {
    OPT_VERSION = 0x100,
    OPT_BUFFER_MS,
    OPT_OUTPUT_DELAY,
    OPT_CONTROL_SOCKET,
    OPT_NO_CONTROL,
    OPT_STATE_DIR,
    OPT_CONFIG,
    OPT_HOOK_START,
    OPT_HOOK_STOP,
    OPT_AUTO_CONNECT,
    OPT_DISCOVERABLE,
    OPT_ALLOW_UNPAIRED,
    OPT_REQUIRE_CONFIRM,
    OPT_NO_DBUS,
    OPT_FIXED_VOLUME,
    OPT_ALSA_MIXER,
    OPT_ALSA_MIXER_CURVE,
    OPT_RELEASE_DEVICE,
};

struct SettableOption {
    Opt opt;
    const char* key;
    const char* flag;
};

const std::vector<SettableOption>& settable_options() {
    static const std::vector<SettableOption> table = {
        {Opt::Device, "output", "-o"},
        {Opt::Name, "name", "-n"},
        {Opt::Adapter, "adapter", "-a"},
        {Opt::TargetDevice, "device", "-D"},
        {Opt::AutoConnect, "auto-connect", "--auto-connect"},
        {Opt::Discoverable, "discoverable", "--discoverable"},
        {Opt::AllowUnpaired, "allow-unpaired", "--allow-unpaired"},
        {Opt::RequireConfirm, "require-confirm", "--require-confirm"},
        {Opt::NoDbus, "no-dbus", "--no-dbus"},
        {Opt::Pidfile, "pidfile", "-P"},
        {Opt::Logfile, "logfile", "-f"},
        {Opt::LogLevel, "log-level", "-d"},
        {Opt::BufferMs, "buffer-ms", "--buffer-ms"},
        {Opt::OutputDelay, "output-delay", "--output-delay"},
        {Opt::ControlSocket, "control-socket", "--control-socket"},
        {Opt::NoControl, "no-control", "--no-control"},
        {Opt::StateDir, "state-dir", "--state-dir"},
        {Opt::HookStart, "hook-start", "--hook-start"},
        {Opt::HookStop, "hook-stop", "--hook-stop"},
        {Opt::FixedVolume, "fixed-volume", "--fixed-volume"},
        {Opt::AlsaMixer, "alsa-mixer", "--alsa-mixer"},
        {Opt::AlsaMixerCurve, "alsa-mixer-curve", "--alsa-mixer-curve"},
        {Opt::ReleaseDevice, "release-device", "--release-device"},
    };
    return table;
}

const SettableOption& settable_option(Opt opt) {
    static const SettableOption unmapped{Opt::Config, "", "an option with no config key"};
    for (const SettableOption& option : settable_options()) {
        if (option.opt == opt) {
            return option;
        }
    }
    return unmapped;
}

bool is_all_digits(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return value.find_first_not_of("0123456789") == std::string::npos;
}

bool parse_buffer_ms(const std::string& str, uint32_t& buffer_ms) {
    if (!is_all_digits(str)) {
        return false;
    }
    const unsigned long value = std::strtoul(str.c_str(), nullptr, 10);
    if (value < MIN_BUFFER_MS || value > MAX_BUFFER_MS) {
        return false;
    }
    buffer_ms = static_cast<uint32_t>(value);
    return true;
}

bool parse_output_delay(const std::string& str, uint16_t& delay_ms) {
    if (!is_all_digits(str)) {
        return false;
    }
    const unsigned long value = std::strtoul(str.c_str(), nullptr, 10);
    if (value > MAX_OUTPUT_DELAY_MS) {
        return false;
    }
    delay_ms = static_cast<uint16_t>(value);
    return true;
}

std::string offending_option(char* const argv[], int index) {
    const char* word = argv[index - 1];
    const bool is_long = word[0] == '-' && word[1] == '-';
    if (!is_long && optopt != 0) {
        return std::string("-") + static_cast<char>(optopt);
    }
    return word;
}

bool parse_log_level(const char* str, LogLevel& level) {
    if (std::strcmp(str, "none") == 0 || std::strcmp(str, "off") == 0) {
        level = LogLevel::NONE;
    } else if (std::strcmp(str, "error") == 0 || std::strcmp(str, "err") == 0) {
        level = LogLevel::ERROR;
    } else if (std::strcmp(str, "warn") == 0 || std::strcmp(str, "warning") == 0) {
        level = LogLevel::WARN;
    } else if (std::strcmp(str, "info") == 0) {
        level = LogLevel::INFO;
    } else if (std::strcmp(str, "debug") == 0) {
        level = LogLevel::DEBUG;
    } else if (std::strcmp(str, "verbose") == 0 || std::strcmp(str, "sdebug") == 0) {
        level = LogLevel::VERBOSE;
    } else {
        return false;
    }
    return true;
}

bool parse_log_spec(const char* spec, LogLevel& level, std::FILE* err) {
    const char* eq = std::strchr(spec, '=');
    if (eq == nullptr) {
        return parse_log_level(spec, level);
    }
    if (!parse_log_level(eq + 1, level)) {
        return false;
    }
    if (err == nullptr) {
        return true;
    }
    std::string tags;
    for (const char* tag : LOG_TAGS) {
        if (!tags.empty()) {
            tags += ", ";
        }
        tags += tag;
    }
    std::fprintf(err,
                 "warning: -d category '%.*s' ignored -- filter by tag (%s) with grep\n",
                 static_cast<int>(eq - spec), spec, tags.c_str());
    return true;
}

bool parse_bool(const std::string& value, bool& result) {
    if (value == "true" || value == "yes" || value == "on" || value == "1") {
        result = true;
        return true;
    }
    if (value == "false" || value == "no" || value == "off" || value == "0") {
        result = false;
        return true;
    }
    return false;
}

bool apply_option(const SettableOption& option, const std::string& value, Options& out,
                  std::string& error, std::FILE* err) {
    const auto empty_value = [&option, &error, &value]() {
        if (!value.empty()) {
            return false;
        }
        error = std::string(option.flag) + " needs a non-empty value";
        return true;
    };

    switch (option.opt) {
        case Opt::Device:
            if (empty_value()) {
                return false;
            }
            out.device = value;
            break;
        case Opt::Name:
            if (empty_value()) {
                return false;
            }
            out.name = value;
            break;
        case Opt::Adapter:
            if (empty_value()) {
                return false;
            }
            out.adapter = value;
            break;
        case Opt::TargetDevice: {
            std::string norm;
            if (!normalize_mac_address(value, norm)) {
                error = "invalid --device '" + value +
                        "' -- expected a Bluetooth MAC address (XX:XX:XX:XX:XX:XX)";
                return false;
            }
            out.target_device = norm;
            break;
        }
        case Opt::AutoConnect:
            if (!parse_bool(value, out.auto_connect)) {
                error = "invalid --auto-connect '" + value + "' -- expected true or false";
                return false;
            }
            break;
        case Opt::Discoverable:
            if (!parse_bool(value, out.discoverable)) {
                error = "invalid --discoverable '" + value + "' -- expected true or false";
                return false;
            }
            break;
        case Opt::AllowUnpaired: {
            bool val = true;
            if (!parse_bool(value, val)) {
                error = "invalid --allow-unpaired '" + value + "' -- expected true or false";
                return false;
            }
            out.allow_unpaired = val;
            break;
        }
        case Opt::RequireConfirm: {
            bool val = false;
            if (!parse_bool(value, val)) {
                error = "invalid --require-confirm '" + value + "' -- expected true or false";
                return false;
            }
            out.allow_unpaired = !val;
            break;
        }
        case Opt::NoDbus:
            if (!parse_bool(value, out.no_dbus)) {
                error = "invalid --no-dbus '" + value + "' -- expected true or false";
                return false;
            }
            break;
        case Opt::Pidfile:
            if (empty_value()) {
                return false;
            }
            out.pidfile = value;
            break;
        case Opt::Logfile:
            if (empty_value()) {
                return false;
            }
            out.logfile = value;
            break;
        case Opt::LogLevel:
            if (!parse_log_spec(value.c_str(), out.log_level, err)) {
                error = "unknown log level '" + value + "'";
                return false;
            }
            break;
        case Opt::BufferMs:
            if (!parse_buffer_ms(value, out.buffer_ms)) {
                error = "invalid --buffer-ms '" + value + "' -- expected " +
                        std::to_string(MIN_BUFFER_MS) + "-" + std::to_string(MAX_BUFFER_MS);
                return false;
            }
            break;
        case Opt::OutputDelay:
            if (!parse_output_delay(value, out.output_delay_ms)) {
                error = "invalid --output-delay '" + value + "' -- expected 0-" +
                        std::to_string(MAX_OUTPUT_DELAY_MS);
                return false;
            }
            break;
        case Opt::ControlSocket:
            if (empty_value()) {
                return false;
            }
            out.control_socket = value;
            break;
        case Opt::NoControl:
            if (!parse_bool(value, out.no_control)) {
                error = "invalid --no-control '" + value + "' -- expected true or false";
                return false;
            }
            break;
        case Opt::StateDir:
            if (empty_value()) {
                return false;
            }
            out.state_dir = value;
            break;
        case Opt::HookStart:
            if (empty_value()) {
                return false;
            }
            out.hook_start = value;
            break;
        case Opt::HookStop:
            if (empty_value()) {
                return false;
            }
            out.hook_stop = value;
            break;
        case Opt::FixedVolume:
            if (!parse_bool(value, out.fixed_volume)) {
                error = "invalid --fixed-volume '" + value + "' -- expected true or false";
                return false;
            }
            break;
        case Opt::AlsaMixer:
            if (empty_value()) {
                return false;
            }
            out.alsa_mixer = value;
            break;
        case Opt::AlsaMixerCurve:
            if (value != "spec" && value != "cubic") {
                error = "invalid --alsa-mixer-curve '" + value + "' -- expected spec or cubic";
                return false;
            }
            out.alsa_mixer_curve = value;
            break;
        case Opt::ReleaseDevice:
            if (!parse_bool(value, out.release_device)) {
                error = "invalid --release-device '" + value + "' -- expected true or false";
                return false;
            }
            break;
        case Opt::ListDevices:
        case Opt::Daemonize:
        case Opt::Config:
            error = "internal: " + std::string(option.flag) + " cannot be set this way";
            return false;
    }
    out.mark_given(option.opt);
    return true;
}

bool merge_config(const ConfigFile& config, bool subcommand_run, Options& out,
                  std::map<Opt, std::string>& origin, std::string& error, std::FILE* err) {
    const auto option_for = [](const std::string& key) -> const SettableOption* {
        for (const SettableOption& candidate : settable_options()) {
            if (key == candidate.key) {
                return &candidate;
            }
        }
        return nullptr;
    };

    std::map<Opt, size_t> last_line;
    for (const KeyValueEntry& entry : config.entries) {
        if (const SettableOption* option = option_for(entry.key)) {
            last_line[option->opt] = entry.line;
        }
    }

    for (const KeyValueEntry& entry : config.entries) {
        const std::string where = config.path + ":" + std::to_string(entry.line) + ": ";

        const SettableOption* option = option_for(entry.key);
        if (option == nullptr) {
            error = where + "unknown key '" + entry.key + "'";
            return false;
        }
        if (entry.line != last_line[option->opt]) {
            continue;
        }
        if (out.was_given(option->opt)) {
            continue;
        }
        const bool applies = !subcommand_run || option->opt == Opt::ControlSocket;
        Options scratch;
        Options& target = applies ? out : scratch;

        std::string message;
        if (!apply_option(*option, entry.value, target, message, applies ? err : nullptr)) {
            error = where + message;
            return false;
        }
        if (applies) {
            origin[option->opt] = where;
        }
    }
    return true;
}

std::string absolute_path(const std::string& path) {
    if (!path.empty() && path.front() == '/') {
        return path;
    }
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        return path;
    }
    return std::string(cwd) + "/" + path;
}

constexpr size_t USAGE_WIDTH = 79;

void print_wrapped(std::FILE* out, const char* text, size_t indent) {
    size_t column = indent;
    const char* word = text;
    while (*word != '\0') {
        const char* end = std::strchr(word, ' ');
        const size_t length = end == nullptr ? std::strlen(word) : static_cast<size_t>(end - word);
        if (column > indent && column + 1 + length > USAGE_WIDTH) {
            std::fprintf(out, "\n%*s", static_cast<int>(indent), "");
            column = indent;
        } else if (column > indent) {
            std::fputc(' ', out);
            ++column;
        }
        std::fprintf(out, "%.*s", static_cast<int>(length), word);
        column += length;
        word = end == nullptr ? word + length : end + 1;
    }
    std::fputc('\n', out);
}

void reset_getopt() {
    optind = 0;
    opterr = 0;
}

}  // namespace

bool parse_options(int argc, char* argv[], Options& out, std::FILE* err) {
    static const struct option long_opts[] = {
        {"help", no_argument, nullptr, 'h'},
        {"version", no_argument, nullptr, OPT_VERSION},
        {"output", required_argument, nullptr, 'o'},
        {"list-devices", no_argument, nullptr, 'l'},
        {"name", required_argument, nullptr, 'n'},
        {"adapter", required_argument, nullptr, 'a'},
        {"device", required_argument, nullptr, 'D'},
        {"pidfile", required_argument, nullptr, 'P'},
        {"logfile", required_argument, nullptr, 'f'},
        {"log-level", required_argument, nullptr, 'd'},
        {"config", required_argument, nullptr, OPT_CONFIG},
        {"buffer-ms", required_argument, nullptr, OPT_BUFFER_MS},
        {"output-delay", required_argument, nullptr, OPT_OUTPUT_DELAY},
        {"auto-connect", required_argument, nullptr, OPT_AUTO_CONNECT},
        {"discoverable", required_argument, nullptr, OPT_DISCOVERABLE},
        {"allow-unpaired", no_argument, nullptr, OPT_ALLOW_UNPAIRED},
        {"require-confirm", no_argument, nullptr, OPT_REQUIRE_CONFIRM},
        {"no-dbus", no_argument, nullptr, OPT_NO_DBUS},
        {"control-socket", required_argument, nullptr, OPT_CONTROL_SOCKET},
        {"no-control", no_argument, nullptr, OPT_NO_CONTROL},
        {"state-dir", required_argument, nullptr, OPT_STATE_DIR},
        {"hook-start", required_argument, nullptr, OPT_HOOK_START},
        {"hook-stop", required_argument, nullptr, OPT_HOOK_STOP},
        {"fixed-volume", no_argument, nullptr, OPT_FIXED_VOLUME},
        {"alsa-mixer", required_argument, nullptr, OPT_ALSA_MIXER},
        {"alsa-mixer-curve", required_argument, nullptr, OPT_ALSA_MIXER_CURVE},
        {"release-device", no_argument, nullptr, OPT_RELEASE_DEVICE},
        {nullptr, 0, nullptr, 0},
    };

    ControlInvocation invocation;
    std::string subcommand_error;
    const bool split_ok = split_subcommand(argc, argv, invocation, subcommand_error);
    out.subcommand = invocation.name;
    out.subcommand_args = invocation.args;

    std::vector<char*> flags;
    flags.push_back(argv[0]);
    for (int index = invocation.consumed == 0 ? 1 : invocation.consumed; index < argc; ++index) {
        flags.push_back(argv[index]);
    }
    const int flag_argc = static_cast<int>(flags.size());
    flags.push_back(nullptr);
    char** const flag_argv = flags.data();

    reset_getopt();

    std::string error;
    const auto fail = [&error](std::string message) {
        if (error.empty()) {
            error = std::move(message);
        }
    };
    const auto apply = [&fail, &out, err](Opt opt, const char* value) {
        std::string message;
        if (!apply_option(settable_option(opt), value, out, message, err)) {
            fail(std::move(message));
        }
    };
    const auto require_value = [&fail](const char* flag, const char* value) {
        if (value[0] != '\0') {
            return true;
        }
        fail(std::string(flag) + " needs a non-empty value");
        return false;
    };

    if (!split_ok) {
        fail(std::move(subcommand_error));
    } else if (!out.subcommand.empty()) {
        ControlRequest request;
        std::string request_error;
        if (!parse_control_request(out.subcommand, out.subcommand_args, request, request_error)) {
            fail(std::move(request_error));
        }
    }

    int opt = 0;
    while ((opt = getopt_long(flag_argc, flag_argv, ":o:ln:a:D:zP:d:f:h", long_opts, nullptr)) !=
           -1) {
        switch (opt) {
            case 'o':
                apply(Opt::Device, optarg);
                break;
            case 'l':
                out.list_devices = true;
                out.mark_given(Opt::ListDevices);
                break;
            case 'n':
                apply(Opt::Name, optarg);
                break;
            case 'a':
                apply(Opt::Adapter, optarg);
                break;
            case 'D':
                apply(Opt::TargetDevice, optarg);
                break;
            case 'z':
                out.daemonize = true;
                out.mark_given(Opt::Daemonize);
                break;
            case 'P':
                apply(Opt::Pidfile, optarg);
                break;
            case 'd':
                apply(Opt::LogLevel, optarg);
                break;
            case 'f':
                apply(Opt::Logfile, optarg);
                break;
            case 'h':
                out.show_help = true;
                return true;
            case OPT_VERSION:
                out.show_version = true;
                return true;
            case OPT_CONFIG:
                if (require_value("--config", optarg)) {
                    out.config_path = optarg;
                    out.mark_given(Opt::Config);
                }
                break;
            case OPT_BUFFER_MS:
                apply(Opt::BufferMs, optarg);
                break;
            case OPT_OUTPUT_DELAY:
                apply(Opt::OutputDelay, optarg);
                break;
            case OPT_AUTO_CONNECT:
                apply(Opt::AutoConnect, optarg);
                break;
            case OPT_DISCOVERABLE:
                apply(Opt::Discoverable, optarg);
                break;
            case OPT_ALLOW_UNPAIRED:
                apply(Opt::AllowUnpaired, "true");
                break;
            case OPT_REQUIRE_CONFIRM:
                apply(Opt::RequireConfirm, "true");
                break;
            case OPT_NO_DBUS:
                apply(Opt::NoDbus, "true");
                break;
            case OPT_CONTROL_SOCKET:
                apply(Opt::ControlSocket, optarg);
                break;
            case OPT_NO_CONTROL:
                apply(Opt::NoControl, "true");
                break;
            case OPT_STATE_DIR:
                apply(Opt::StateDir, optarg);
                break;
            case OPT_HOOK_START:
                apply(Opt::HookStart, optarg);
                break;
            case OPT_HOOK_STOP:
                apply(Opt::HookStop, optarg);
                break;
            case OPT_FIXED_VOLUME:
                apply(Opt::FixedVolume, "true");
                break;
            case OPT_ALSA_MIXER:
                apply(Opt::AlsaMixer, optarg);
                break;
            case OPT_ALSA_MIXER_CURVE:
                apply(Opt::AlsaMixerCurve, optarg);
                break;
            case OPT_RELEASE_DEVICE:
                apply(Opt::ReleaseDevice, "true");
                break;
            case ':':
                fail("option '" + offending_option(flag_argv, optind) + "' needs a value");
                break;
            case '?':
            default:
                fail("unknown option '" + offending_option(flag_argv, optind) + "'");
                break;
        }
    }

    if (optind < flag_argc) {
        const std::string word = flag_argv[optind];
        if (find_control_subcommand(word) != nullptr) {
            fail("a subcommand has to come first: '" + std::string(argv[0]) + " " + word +
                 " [flags]', not after the flags");
        } else {
            fail("unexpected argument '" + word +
                 "' -- this daemon takes flags and one optional subcommand (" +
                 control_subcommand_list() + ")");
        }
    }

    std::map<Opt, std::string> config_origin;
    if (error.empty() && !out.list_devices) {
        ConfigFile config;
        std::string reason;
        if (!load_config_file(out.was_given(Opt::Config) ? out.config_path : std::string(),
                              config_search_paths(), config, reason)) {
            fail(std::move(reason));
        } else {
            out.config_path = config.path;
            if (!merge_config(config, !out.subcommand.empty(), out, config_origin, reason, err)) {
                fail(std::move(reason));
            }
        }
    }

    if (error.empty() && out.was_given(Opt::AllowUnpaired) && out.was_given(Opt::RequireConfirm)) {
        fail("--allow-unpaired and --require-confirm contradict each other -- drop one");
    }

    if (error.empty() && out.was_given(Opt::Daemonize) && out.was_given(Opt::Device)) {
        DeviceSpec spec;
        std::string reason;
        if (resolve_device_spec(out.device, spec, reason) && spec.backend == SinkBackend::Stdout) {
            fail("-z cannot write PCM to stdout: a daemon's stdout is /dev/null, so -o '" +
                 out.device + "' would discard every stream. Drop -z, or pick a real device.");
        }
    }

    if (error.empty() && out.was_given(Opt::NoControl) && out.was_given(Opt::ControlSocket)) {
        fail("--no-control and --control-socket '" + out.control_socket +
             "' contradict each other -- drop one");
    }

    if (error.empty() && out.was_given(Opt::AlsaMixer)) {
        DeviceSpec spec;
        std::string reason;
        if (resolve_device_spec(out.device, spec, reason) && spec.backend != SinkBackend::Alsa) {
            fail("--alsa-mixer '" + out.alsa_mixer +
                 "' needs -o alsa:<device> -- no other backend has a hardware mixer");
        }
        if (out.fixed_volume) {
            fail("--alsa-mixer and --fixed-volume contradict each other");
        }
    }

    if (error.empty() && out.was_given(Opt::AlsaMixerCurve) && !out.was_given(Opt::AlsaMixer)) {
        fail("--alsa-mixer-curve '" + out.alsa_mixer_curve + "' needs --alsa-mixer");
    }

    if (error.empty()) {
        if (out.no_control && out.subcommand.empty()) {
            out.control_socket.clear();
        } else if (out.was_given(Opt::ControlSocket)) {
            if (out.was_given(Opt::Daemonize)) {
                out.control_socket = absolute_path(out.control_socket);
            }
            if (!control_socket_path_fits(out.control_socket)) {
                fail("--control-socket '" + out.control_socket + "' is " +
                     std::to_string(out.control_socket.size()) +
                     " bytes, and a Unix socket address holds at most " +
                     std::to_string(control_socket_path_limit() - 1) + " on this platform");
            }
        } else {
            const ControlRuntimeDir runtime = control_runtime_dir();
            out.control_socket = control_socket_path(runtime.path);
            out.control_absent_reason = control_socket_absent_reason(runtime, out.control_socket);
            if (!out.control_absent_reason.empty()) {
                out.control_socket.clear();
            }
            if (!runtime.warning.empty() && out.subcommand.empty()) {
                std::fprintf(err, "warning: %s\n", runtime.warning.c_str());
            }
        }
    }

    if (!error.empty()) {
        std::fprintf(err, "error: %s\n", error.c_str());
        return false;
    }

    if (!out.subcommand.empty()) {
        static constexpr Opt DAEMON_ONLY[] = {
            Opt::Device,         Opt::Name,          Opt::Adapter,     Opt::TargetDevice,
            Opt::AutoConnect,    Opt::Discoverable,  Opt::AllowUnpaired, Opt::RequireConfirm,
            Opt::NoDbus,         Opt::Daemonize,     Opt::Pidfile,     Opt::Logfile,
            Opt::LogLevel,       Opt::BufferMs,      Opt::NoControl,   Opt::StateDir,
            Opt::OutputDelay,    Opt::HookStart,     Opt::HookStop,    Opt::FixedVolume,
            Opt::AlsaMixer,      Opt::AlsaMixerCurve, Opt::ReleaseDevice,
        };
        for (Opt opt_item : DAEMON_ONLY) {
            if (out.was_given(opt_item)) {
                std::fprintf(err,
                             "warning: a subcommand reads only --control-socket -- the "
                             "other flags configure the daemon and do nothing here\n");
                break;
            }
        }
    } else {
        if (out.was_given(Opt::Daemonize) && !out.was_given(Opt::Logfile)) {
            std::fprintf(err,
                         "warning: -z without -f discards all log output -- a detached daemon's "
                         "stderr is /dev/null. Add -f <path> to keep it.\n");
        }
    }

    if (out.was_given(Opt::Daemonize)) {
        if (out.was_given(Opt::Pidfile)) {
            out.pidfile = absolute_path(out.pidfile);
        }
        if (out.was_given(Opt::Logfile)) {
            out.logfile = absolute_path(out.logfile);
        }
        if (out.was_given(Opt::StateDir)) {
            out.state_dir = absolute_path(out.state_dir);
        }
    }

    if (out.name.empty()) {
        out.name = default_speaker_name();
    }
    return true;
}

void print_usage(std::FILE* out, const char* prog) {
    std::fprintf(out, "Usage: %s [options]\n", prog);
    std::fprintf(out, "       %s <subcommand> [args] [--control-socket <path>]\n\n", prog);
    std::fprintf(out,
                 "A headless Bluetooth A2DP speaker daemon. Bridges incoming Bluetooth audio\n"
                 "from a PC or phone into PulseAudio (or PipeWire/ALSA) with active wait and\n"
                 "automatic reconnection.\n\n");
    std::fprintf(out, "Subcommands:\n");
    for (const ControlSubcommand& subcommand : control_subcommands()) {
        std::string invocation = subcommand.name;
        if (subcommand.argument != nullptr) {
            invocation += " ";
            invocation += subcommand.argument;
        }
        std::fprintf(out, "  %-22s", invocation.c_str());
        print_wrapped(out, subcommand.description, 24);
    }
    std::fprintf(out, "\nOptions:\n");
    std::fprintf(out, "  Every option below except -l, -z, --config, --help and --version can\n");
    std::fprintf(out, "  also be set in a config file, one 'key = value' per line:\n");
    for (const std::string& path : config_search_paths()) {
        std::fprintf(out, "    %s\n", path.c_str());
    }
    std::fprintf(out, "\n");
    std::fprintf(out, "  -o, --output <device>   Output device (default: %s; backends: %s)\n",
                 DEFAULT_OUTPUT_DEVICE, audio_backend_list().c_str());
    std::fprintf(out, "  -l, --list-devices      List output devices and Bluetooth sources\n");
    std::fprintf(out, "  -n, --name <name>       Bluetooth alias shown to peers (default: <host>-bt)\n");
    std::fprintf(out, "  -a, --adapter <hci|MAC> BlueZ adapter to use (default: first available)\n");
    std::fprintf(out, "  -D, --device <MAC>      Restrict/prioritize a remote peer MAC\n");
    std::fprintf(out, "  --auto-connect <bool>   Auto-reconnect to last trusted peer (default: true)\n");
    std::fprintf(out, "  --discoverable <bool>   Keep Bluetooth discoverable (default: true)\n");
    std::fprintf(out, "  --allow-unpaired        Auto-accept pairing requests (default)\n");
    std::fprintf(out, "  --require-confirm       Wait for 'bt-speaker-cli pair confirm'\n");
    std::fprintf(out, "  --no-dbus               Disable BlueZ D-Bus control (testing)\n");
    std::fprintf(out, "  --buffer-ms <ms>        Output buffer, %u-%u ms (default: %u)\n",
                 MIN_BUFFER_MS, MAX_BUFFER_MS, DEFAULT_BUFFER_MS);
    std::fprintf(out, "  --output-delay <ms>     Output delay metadata, 0-%u ms (default: 0)\n",
                 MAX_OUTPUT_DELAY_MS);
    std::fprintf(out, "  --fixed-volume          Lock output at 100%% volume, unmuted\n");
    std::fprintf(out, "  --alsa-mixer <card:ctl> Hardware ALSA mixer for volume/mute\n");
    std::fprintf(out, "  --alsa-mixer-curve <c>  Mixer dB curve: spec (default) or cubic\n");
    std::fprintf(out, "  --release-device        Close output device when stream ends\n");
    std::fprintf(out, "  --hook-start <cmd>      Shell command run when BT audio stream starts\n");
    std::fprintf(out, "  --hook-stop <cmd>       Shell command run when BT audio stream stops\n");
    std::fprintf(out, "  --control-socket <path> Unix socket path (default: $XDG_RUNTIME_DIR/%s)\n",
                 CONTROL_SOCKET_FILENAME);
    std::fprintf(out, "  --no-control            Disable control socket\n");
    std::fprintf(out, "  --state-dir <dir>       Persistent state directory\n");
    std::fprintf(out, "  --config <path>         Explicit config file path\n");
    std::fprintf(out, "  -z                      Fork and detach into background\n");
    std::fprintf(out, "  -P, --pidfile <path>    Locked pidfile path\n");
    std::fprintf(out, "  -f, --logfile <path>    Log file path (supports SIGHUP rotation)\n");
    std::fprintf(out, "  -d, --log-level <lvl>   none, error, warn, info, debug, verbose\n");
    std::fprintf(out, "  -h, --help              Show this help\n");
    std::fprintf(out, "  --version               Show version information\n");
}

void print_version(std::FILE* out) {
    std::fprintf(out, "bt-speaker-cli %s\n", BT_SPEAKER_CLI_VERSION);
}

std::string default_speaker_name() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return FALLBACK_NAME;
    }
    hostname[sizeof(hostname) - 1] = '\0';
    return hostname[0] == '\0' ? FALLBACK_NAME : std::string(hostname) + "-bt";
}

}  // namespace bt_speaker_cli
