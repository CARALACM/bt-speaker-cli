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

/// Local control channel: a Unix socket in the daemon, subcommands on the same binary.

#pragma once

#include "audio_sink.h"
#include "bt_reconnect.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bt_speaker_cli {

inline constexpr const char* CONTROL_SOCKET_FILENAME = "bt-speaker-cli.sock";
inline constexpr size_t MAX_CONTROL_LINE_BYTES = 256;
inline constexpr size_t MAX_CONTROL_CONNECTIONS = 8;
inline constexpr int64_t CONTROL_IDLE_TIMEOUT_MS = 5000;
inline constexpr uint16_t MAX_OUTPUT_DELAY_MS = 5000;

enum class ControlCommand : uint8_t {
    Status,
    Play,
    Pause,
    Stop,
    Next,
    Previous,
    Volume,
    Mute,
    Delay,
    Connect,
    Disconnect,
    Devices,
    Trust,
    Forget,
    Discoverable,
    Pair,
};

enum class ControlStatus : uint8_t {
    Ok = 0,
    Usage = 2,
    NoDaemon = 3,
    NotConnected = 4,
    Unsupported = 5,
    Failed = 6,
};

struct ControlSubcommand {
    const char* name;
    ControlCommand command;
    unsigned min_arity;
    unsigned max_arity;
    const char* argument;
    const char* description;
};

const std::vector<ControlSubcommand>& control_subcommands();
const ControlSubcommand* find_control_subcommand(const std::string& name);
std::string control_subcommand_list();

struct ControlRequest {
    ControlCommand command{ControlCommand::Status};
    std::optional<uint8_t> volume{};
    std::optional<bool> flag{};
    std::optional<uint16_t> delay_ms{};
    std::optional<bool> confirm{};
    std::string mac{};
};

struct ControlInvocation {
    std::string name;
    std::vector<std::string> args;
    int consumed{0};
};

bool split_subcommand(int argc, char* const argv[], ControlInvocation& out, std::string& error);
bool parse_control_request(const std::string& name, const std::vector<std::string>& args,
                           ControlRequest& out, std::string& error);
std::string encode_control_request(const ControlRequest& request);
bool split_control_line(const std::string& line, std::string& name, std::vector<std::string>& args);

struct StatusSnapshot {
    std::string name;
    bool dbus_available{false};
    std::optional<BtAdapterInfo> adapter{};
    std::optional<BtDeviceInfo> connected_device{};
    bool discoverable{false};

    bool pairing_active{false};
    std::string pairing_device;
    std::string pairing_passkey;

    bool streaming{false};
    std::string bt_source;
    std::optional<StreamFormat> format{};

    BtMediaInfo media{};

    uint8_t volume{DEFAULT_SINK_VOLUME};
    bool muted{false};
    VolumeSource volume_source{VolumeSource::SinkDefault};
    uint16_t output_delay_ms{0};
    std::string output;
};

std::string format_status(const StatusSnapshot& snapshot);
std::string format_device_list(const std::vector<BtDeviceInfo>& devices);

std::string encode_control_reply(ControlStatus status, const std::string& reason,
                                 const std::string& payload);
bool decode_control_reply(const std::string& line, ControlStatus& status, std::string& reason);

enum class LineState {
    Incomplete,
    Ready,
    TooLong,
    Invalid,
};

class LineAssembler {
public:
    LineState feed(const char* data, size_t length);
    LineState finish();
    const std::string& line() const {
        return this->line_;
    }

private:
    std::string buffer_;
    std::string line_;
};

const char* line_state_reason(LineState state);

std::string control_socket_path(const std::string& runtime_dir);

struct ControlRuntimeDir {
    std::string path;
    std::string warning;
    std::string rejection;
};

ControlRuntimeDir control_runtime_dir();
bool is_private_runtime_dir(const std::string& path, std::string& reason);
std::string control_socket_absent_reason(const ControlRuntimeDir& runtime, const std::string& path);
bool control_socket_path_fits(const std::string& path);
size_t control_socket_path_limit();

enum class ControlSocketStatus {
    Ok,
    AlreadyRunning,
    Failed,
};

ControlSocketStatus probe_control_socket(const std::string& path, std::string& error);

class ControlHandler {
public:
    virtual ~ControlHandler() = default;
    virtual std::string handle_control_request(const std::string& line) = 0;
};

class ControlSocket {
public:
    ControlSocket();
    ~ControlSocket();

    ControlSocket(const ControlSocket&) = delete;
    ControlSocket& operator=(const ControlSocket&) = delete;

    ControlSocketStatus open(const std::string& path, std::string& error);
    void poll(int64_t now_ms, ControlHandler& handler);
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

ControlStatus run_control_subcommand(const ControlRequest& request, const std::string& path,
                                     const std::string& absent_reason, std::FILE* out);

}  // namespace bt_speaker_cli
