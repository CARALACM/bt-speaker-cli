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

#include "control.h"

#include "socket_compat.h"

#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace bt_speaker_cli {

namespace {

constexpr const char* ON_OFF = "on|off";
constexpr const char* CONFIRM_CANCEL = "confirm|cancel";

struct ReplyKind {
    ControlStatus status;
    const char* token;
};
constexpr ReplyKind REPLY_KINDS[] = {
    {ControlStatus::Usage, "usage"},
    {ControlStatus::NotConnected, "not-connected"},
    {ControlStatus::Unsupported, "unsupported"},
    {ControlStatus::Failed, "failed"},
};

bool parse_unsigned(const std::string& text, uint64_t max, uint64_t& value) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return false;
    }
    errno = 0;
    const unsigned long long parsed = std::strtoull(text.c_str(), nullptr, 10);
    if (errno == ERANGE || parsed > max) {
        return false;
    }
    value = parsed;
    return true;
}

bool parse_on_off(const std::string& text, bool& value) {
    if (text == "on") {
        value = true;
        return true;
    }
    if (text == "off") {
        value = false;
        return true;
    }
    return false;
}

std::string command_name(ControlCommand command) {
    for (const ControlSubcommand& subcommand : control_subcommands()) {
        if (subcommand.command == command) {
            return subcommand.name;
        }
    }
    return "?";
}

std::string format_clock(uint32_t milliseconds) {
    const uint32_t total_seconds = milliseconds / 1000U;
    const uint32_t hours = total_seconds / 3600U;
    const uint32_t minutes = (total_seconds % 3600U) / 60U;
    const uint32_t seconds = total_seconds % 60U;

    const auto two_digits = [](uint32_t value) {
        return (value < 10 ? std::string("0") : std::string()) + std::to_string(value);
    };
    if (hours > 0) {
        return std::to_string(hours) + ":" + two_digits(minutes) + ":" + two_digits(seconds);
    }
    return std::to_string(minutes) + ":" + two_digits(seconds);
}

void append_line(std::string& out, const char* key, const std::string& value) {
    out += key;
    out += ": ";
    out += value;
    out += "\n";
}

std::string format_volume(uint8_t volume, bool muted) {
    return std::to_string(static_cast<unsigned>(volume)) + (muted ? " (muted)" : "");
}

const char* volume_source_note(VolumeSource source) {
    switch (source) {
        case VolumeSource::Restored:
            return " (restored from state file)";
        case VolumeSource::Remote:
            return " (synced from remote peer)";
        case VolumeSource::Fixed:
            return " (fixed)";
        case VolumeSource::Hardware:
            return " (hardware mixer)";
        case VolumeSource::SinkDefault:
            break;
    }
    return "";
}

}  // namespace

const std::vector<ControlSubcommand>& control_subcommands() {
    static const std::vector<ControlSubcommand> table = {
        {"status", ControlCommand::Status, 0, 0, nullptr,
         "Bluetooth adapter, connected peer, stream and audio output status"},
        {"play", ControlCommand::Play, 0, 0, nullptr, "Send AVRCP Play to the connected peer"},
        {"pause", ControlCommand::Pause, 0, 0, nullptr, "Send AVRCP Pause to the connected peer"},
        {"stop", ControlCommand::Stop, 0, 0, nullptr, "Send AVRCP Stop to the connected peer"},
        {"next", ControlCommand::Next, 0, 0, nullptr, "Send AVRCP Next to the connected peer"},
        {"prev", ControlCommand::Previous, 0, 0, nullptr,
         "Send AVRCP Previous to the connected peer"},
        {"vol", ControlCommand::Volume, 1, 1, "<0-100>", "Set local output volume"},
        {"mute", ControlCommand::Mute, 1, 1, ON_OFF, "Mute or unmute local output"},
        {"delay", ControlCommand::Delay, 1, 1, "<0-5000>",
         "Set output delay compensation in milliseconds"},
        {"connect", ControlCommand::Connect, 0, 1, "[<MAC>]",
         "Connect to <MAC>, or the last paired Bluetooth device"},
        {"disconnect", ControlCommand::Disconnect, 0, 1, "[<MAC>]",
         "Disconnect the active Bluetooth device"},
        {"devices", ControlCommand::Devices, 0, 0, nullptr,
         "List paired/known Bluetooth devices"},
        {"trust", ControlCommand::Trust, 1, 1, "<MAC>", "Mark a Bluetooth device as trusted"},
        {"forget", ControlCommand::Forget, 1, 1, "<MAC>", "Remove a paired Bluetooth device"},
        {"discoverable", ControlCommand::Discoverable, 1, 1, ON_OFF,
         "Enable or disable Bluetooth discoverability"},
        {"pair", ControlCommand::Pair, 1, 1, CONFIRM_CANCEL,
         "Confirm or cancel a pending pairing request"},
    };
    return table;
}

const ControlSubcommand* find_control_subcommand(const std::string& name) {
    for (const ControlSubcommand& subcommand : control_subcommands()) {
        if (name == subcommand.name) {
            return &subcommand;
        }
    }
    return nullptr;
}

std::string control_subcommand_list() {
    std::string list;
    for (const ControlSubcommand& subcommand : control_subcommands()) {
        if (!list.empty()) {
            list += ", ";
        }
        list += subcommand.name;
    }
    return list;
}

bool split_subcommand(int argc, char* const argv[], ControlInvocation& out, std::string& error) {
    out = ControlInvocation{};
    if (argc < 2 || argv[1][0] == '-') {
        return true;
    }

    const ControlSubcommand* subcommand = find_control_subcommand(argv[1]);
    if (subcommand == nullptr) {
        error = "unknown subcommand '" + std::string(argv[1]) +
                "' -- expected one of: " + control_subcommand_list();
        return false;
    }

    if (static_cast<unsigned>(argc) < 2U + subcommand->min_arity) {
        error = std::string("'") + subcommand->name + "' needs an argument: " + subcommand->name +
                " " + subcommand->argument;
        return false;
    }

    out.name = subcommand->name;
    unsigned taken = 0;
    while (taken < subcommand->max_arity && (2U + taken) < static_cast<unsigned>(argc)) {
        const char* candidate = argv[2U + taken];
        // Stop before flags when the argument is optional.
        if (taken >= subcommand->min_arity && candidate[0] == '-') {
            break;
        }
        out.args.emplace_back(candidate);
        ++taken;
    }
    out.consumed = static_cast<int>(2U + taken);
    return true;
}

bool parse_control_request(const std::string& name, const std::vector<std::string>& args,
                           ControlRequest& out, std::string& error) {
    const ControlSubcommand* subcommand = find_control_subcommand(name);
    if (subcommand == nullptr) {
        error =
            "unknown subcommand '" + name + "' -- expected one of: " + control_subcommand_list();
        return false;
    }
    if (args.size() < subcommand->min_arity || args.size() > subcommand->max_arity) {
        error = std::string("'") + subcommand->name + "' takes " +
                std::to_string(subcommand->min_arity) +
                (subcommand->max_arity > subcommand->min_arity
                     ? "-" + std::to_string(subcommand->max_arity)
                     : "") +
                " argument(s), got " + std::to_string(args.size());
        return false;
    }

    out = ControlRequest{};
    out.command = subcommand->command;
    if (args.empty()) {
        return true;
    }

    const std::string& value = args.front();
    const auto reject = [&error, subcommand, &value](const std::string& accepted) {
        error = std::string("'") + subcommand->name + " " + value + "': expected " + accepted;
        return false;
    };

    switch (subcommand->command) {
        case ControlCommand::Volume: {
            uint64_t volume = 0;
            if (!parse_unsigned(value, 100, volume)) {
                return reject("a volume from 0 to 100");
            }
            out.volume = static_cast<uint8_t>(volume);
            return true;
        }
        case ControlCommand::Delay: {
            uint64_t delay = 0;
            if (!parse_unsigned(value, MAX_OUTPUT_DELAY_MS, delay)) {
                return reject("an output delay in milliseconds, from 0 to " +
                              std::to_string(MAX_OUTPUT_DELAY_MS));
            }
            out.delay_ms = static_cast<uint16_t>(delay);
            return true;
        }
        case ControlCommand::Mute:
        case ControlCommand::Discoverable: {
            bool flag = false;
            if (!parse_on_off(value, flag)) {
                return reject("'on' or 'off'");
            }
            out.flag = flag;
            return true;
        }
        case ControlCommand::Pair: {
            if (value != "confirm" && value != "cancel") {
                return reject("'confirm' or 'cancel'");
            }
            out.confirm = (value == "confirm");
            return true;
        }
        case ControlCommand::Connect:
        case ControlCommand::Disconnect:
        case ControlCommand::Trust:
        case ControlCommand::Forget: {
            std::string norm;
            if (!normalize_mac_address(value, norm)) {
                return reject("a Bluetooth MAC address (XX:XX:XX:XX:XX:XX)");
            }
            out.mac = norm;
            return true;
        }
        case ControlCommand::Status:
        case ControlCommand::Play:
        case ControlCommand::Pause:
        case ControlCommand::Stop:
        case ControlCommand::Next:
        case ControlCommand::Previous:
        case ControlCommand::Devices:
            break;
    }
    error = std::string("'") + subcommand->name + "' takes no argument";
    return false;
}

std::string encode_control_request(const ControlRequest& request) {
    std::string line = command_name(request.command);
    if (request.volume.has_value()) {
        line += " " + std::to_string(static_cast<unsigned>(*request.volume));
    } else if (request.flag.has_value()) {
        line += *request.flag ? " on" : " off";
    } else if (request.delay_ms.has_value()) {
        line += " " + std::to_string(static_cast<unsigned>(*request.delay_ms));
    } else if (request.confirm.has_value()) {
        line += *request.confirm ? " confirm" : " cancel";
    } else if (!request.mac.empty()) {
        line += " " + request.mac;
    }
    return line;
}

bool split_control_line(const std::string& line, std::string& name,
                        std::vector<std::string>& args) {
    name.clear();
    args.clear();

    size_t position = 0;
    while (position < line.size()) {
        const size_t start = line.find_first_not_of(" \t", position);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = line.find_first_of(" \t", start);
        std::string word = line.substr(start, end == std::string::npos ? end : end - start);
        if (name.empty()) {
            name = std::move(word);
        } else {
            args.push_back(std::move(word));
        }
        position = end == std::string::npos ? line.size() : end;
    }
    return !name.empty();
}

std::string format_status(const StatusSnapshot& snapshot) {
    std::string out;

    append_line(out, "name", snapshot.name);
    if (!snapshot.dbus_available) {
        append_line(out, "bluetooth", "D-Bus unavailable / disabled");
    } else if (!snapshot.adapter.has_value()) {
        append_line(out, "bluetooth", "waiting for adapter");
    } else {
        append_line(out, "bluetooth",
                    snapshot.adapter->name + " (" + snapshot.adapter->address + "), " +
                        (snapshot.adapter->powered ? "powered" : "off") + ", discoverable: " +
                        (snapshot.discoverable ? "on" : "off"));
    }

    if (snapshot.connected_device.has_value()) {
        const BtDeviceInfo& dev = *snapshot.connected_device;
        append_line(out, "peer",
                    (dev.name.empty() ? "unknown" : dev.name) + " (" + dev.address + ")");
    } else {
        append_line(out, "peer", "waiting for connection");
    }

    if (snapshot.pairing_active) {
        std::string msg = "waiting for confirmation from " + snapshot.pairing_device;
        if (!snapshot.pairing_passkey.empty()) {
            msg += " (passkey " + snapshot.pairing_passkey + ")";
        }
        append_line(out, "pairing", msg);
    }

    append_line(out, "stream", snapshot.streaming ? "receiving" : "idle");
    if (!snapshot.bt_source.empty()) {
        append_line(out, "bt source", snapshot.bt_source);
    }

    if (!snapshot.media.status.empty()) {
        append_line(out, "state", snapshot.media.status);
    } else {
        append_line(out, "state", snapshot.streaming ? "playing" : "idle");
    }

    if (snapshot.media.title.empty() && snapshot.media.artist.empty()) {
        append_line(out, "track", "unknown");
    } else if (snapshot.media.artist.empty()) {
        append_line(out, "track", snapshot.media.title);
    } else if (snapshot.media.title.empty()) {
        append_line(out, "track", snapshot.media.artist);
    } else {
        append_line(out, "track", snapshot.media.artist + " - " + snapshot.media.title);
    }

    if (snapshot.media.position_ms.has_value()) {
        const std::string pos = format_clock(*snapshot.media.position_ms);
        const uint32_t dur = snapshot.media.duration_ms.value_or(0);
        append_line(out, "position", dur == 0 ? pos + " / unknown" : pos + " / " + format_clock(dur));
    }

    append_line(out, "volume",
                format_volume(snapshot.volume, snapshot.muted) +
                    volume_source_note(snapshot.volume_source));
    append_line(out, "output delay",
                std::to_string(static_cast<unsigned>(snapshot.output_delay_ms)) + " ms");

    if (snapshot.format.has_value()) {
        append_line(
            out, "output",
            snapshot.output + " (" + std::to_string(snapshot.format->sample_rate) + " Hz / " +
                std::to_string(static_cast<unsigned>(snapshot.format->channels)) + " ch / " +
                std::to_string(static_cast<unsigned>(snapshot.format->bit_depth)) + "-bit)");
    } else {
        append_line(out, "output", snapshot.output);
    }

    return out;
}

std::string format_device_list(const std::vector<BtDeviceInfo>& devices) {
    if (devices.empty()) {
        return "(no paired or known Bluetooth devices)\n";
    }
    std::string out;
    for (const BtDeviceInfo& dev : devices) {
        out += dev.address;
        out += "  ";
        out += dev.name.empty() ? "(unnamed)" : dev.name;
        out += "  [";
        out += dev.connected ? "connected" : "disconnected";
        if (dev.paired) {
            out += ", paired";
        }
        if (dev.trusted) {
            out += ", trusted";
        }
        out += "]\n";
    }
    return out;
}

std::string encode_control_reply(ControlStatus status, const std::string& reason,
                                 const std::string& payload) {
    if (status == ControlStatus::Ok) {
        return "ok\n" + payload;
    }

    std::string one_line = reason;
    std::replace(one_line.begin(), one_line.end(), '\n', ' ');
    std::replace(one_line.begin(), one_line.end(), '\r', ' ');

    for (const ReplyKind& kind : REPLY_KINDS) {
        if (kind.status == status) {
            return "error " + std::string(kind.token) + ": " + one_line + "\n" + payload;
        }
    }
    return "error failed: " + one_line + "\n" + payload;
}

bool decode_control_reply(const std::string& line, ControlStatus& status, std::string& reason) {
    reason.clear();
    if (line == "ok") {
        status = ControlStatus::Ok;
        return true;
    }

    static constexpr const char* PREFIX = "error ";
    const size_t prefix_length = std::strlen(PREFIX);
    if (line.compare(0, prefix_length, PREFIX) != 0) {
        return false;
    }
    const size_t colon = line.find(':', prefix_length);
    if (colon == std::string::npos) {
        return false;
    }

    const std::string token = line.substr(prefix_length, colon - prefix_length);
    reason = line.substr(colon + 1);
    if (!reason.empty() && reason.front() == ' ') {
        reason.erase(0, 1);
    }
    for (const ReplyKind& kind : REPLY_KINDS) {
        if (token == kind.token) {
            status = kind.status;
            return true;
        }
    }
    status = ControlStatus::Failed;
    return true;
}

LineState LineAssembler::feed(const char* data, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        const char byte = data[index];
        if (byte == '\0') {
            return LineState::Invalid;
        }
        if (byte == '\n') {
            this->line_ = std::move(this->buffer_);
            this->buffer_.clear();
            if (!this->line_.empty() && this->line_.back() == '\r') {
                this->line_.pop_back();
            }
            return LineState::Ready;
        }
        if (this->buffer_.size() >= MAX_CONTROL_LINE_BYTES) {
            return LineState::TooLong;
        }
        this->buffer_ += byte;
    }
    return LineState::Incomplete;
}

LineState LineAssembler::finish() {
    if (this->buffer_.empty()) {
        return LineState::Incomplete;
    }
    this->line_ = std::move(this->buffer_);
    this->buffer_.clear();
    if (!this->line_.empty() && this->line_.back() == '\r') {
        this->line_.pop_back();
    }
    return LineState::Ready;
}

const char* line_state_reason(LineState state) {
    switch (state) {
        case LineState::TooLong:
            return "the request line is too long";
        case LineState::Invalid:
            return "the request contains a NUL byte, and this is a text protocol";
        case LineState::Incomplete:
            return "the connection closed before a whole request arrived";
        case LineState::Ready:
            break;
    }
    return "";
}

bool is_private_runtime_dir(const std::string& path, std::string& reason) {
    struct stat info = {};
    if (::stat(path.c_str(), &info) != 0) {
        reason = "cannot stat " + path + ": " + std::strerror(errno);
        return false;
    }
    if (!S_ISDIR(info.st_mode)) {
        reason = path + " is not a directory";
        return false;
    }
    if (info.st_uid != ::geteuid()) {
        reason = path + " is owned by uid " + std::to_string(info.st_uid) + ", not by this user";
        return false;
    }
    if ((info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        reason = path + " is writable by its group or by everyone";
        return false;
    }
    return true;
}

ControlRuntimeDir control_runtime_dir() {
    ControlRuntimeDir result;

    const char* value = std::getenv("XDG_RUNTIME_DIR");
    if (value != nullptr && value[0] != '\0') {
        result.path = value;
        std::string reason;
        if (!is_private_runtime_dir(result.path, reason)) {
            result.warning =
                "the control socket's directory is not private to this user (" + reason +
                "). Point --control-socket somewhere private, or fix the directory's mode";
        }
        return result;
    }
    return result;
}

std::string control_socket_path(const std::string& runtime_dir) {
    if (runtime_dir.empty()) {
        return {};
    }
    return runtime_dir + "/" + CONTROL_SOCKET_FILENAME;
}

std::string control_socket_absent_reason(const ControlRuntimeDir& runtime,
                                         const std::string& path) {
    if (runtime.path.empty()) {
        if (!runtime.rejection.empty()) {
            return "this host's own per-user directory cannot hold a control socket: " +
                   runtime.rejection +
                   ". Give --control-socket <path> to choose one, or --no-control to stop asking";
        }
        return "$XDG_RUNTIME_DIR is not set, so there is no user-private directory to put a "
               "control socket in. Give --control-socket <path> to choose one, or "
               "--no-control to stop asking";
    }
    if (!control_socket_path_fits(path)) {
        return "the default control socket path '" + path + "' is longer than the " +
               std::to_string(control_socket_path_limit()) +
               " bytes a Unix socket address holds. Give --control-socket <a shorter path>, or "
               "--no-control";
    }
    return {};
}

bool control_socket_path_fits(const std::string& path) {
    return !path.empty() && path.size() < control_socket_path_limit();
}

size_t control_socket_path_limit() {
    return sizeof(sockaddr_un::sun_path);
}

}  // namespace bt_speaker_cli
