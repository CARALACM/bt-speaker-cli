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

/// bt-speaker-cli: headless Bluetooth A2DP speaker daemon and control client.

#ifdef BT_SPEAKER_CLI_HAVE_ALSA
#include "alsa_sink.h"
#endif
#include "audio_sink.h"
#include "bt_audio_bridge.h"
#include "bt_manager.h"
#include "cli.h"
#include "clock.h"
#include "control.h"
#include "daemon.h"
#include "hooks.h"
#include "log.h"
#include "state_store.h"

#include <signal.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <thread>

static constexpr const char* LOG_TAG = bt_speaker_cli::LOG_TAG_CLI;

namespace {

using namespace bt_speaker_cli;  // NOLINT(google-build-using-namespace)

constexpr int LOOP_INTERVAL_MS = 10;

std::atomic<bool> g_running{true};

void handle_signal(int /*sig*/) {
    g_running.store(false);
}

struct VolumeState {
    uint8_t volume{DEFAULT_SINK_VOLUME};
    bool muted{false};
    VolumeSource source{VolumeSource::SinkDefault};
    uint16_t output_delay_ms{0};
    bool fixed{false};

    void apply(AudioSink& sink, StateStore* store, uint8_t new_volume, bool new_muted,
               VolumeSource new_source, bool persist) {
        if (this->fixed) {
            return;
        }
        this->volume = new_volume > 100 ? 100 : new_volume;
        this->muted = new_muted;
        this->source = new_source;
        sink.set_volume(this->volume);
        sink.set_muted(this->muted);
        if (persist && store != nullptr) {
            store->set_volume_and_muted(this->volume, this->muted);
        }
    }
};

class ControlDispatcher final : public ControlHandler {
public:
    ControlDispatcher(const Options& opts, BtManager& bt, BtAudioBridge& bridge, AudioSink& sink,
                      VolumeState& volume_state, StateStore& state_store)
        : opts_(opts),
          bt_(bt),
          bridge_(bridge),
          sink_(sink),
          volume_state_(volume_state),
          state_store_(state_store) {}

    std::string handle_control_request(const std::string& line) override {
        std::string name;
        std::vector<std::string> args;
        if (!split_control_line(line, name, args)) {
            return encode_control_reply(ControlStatus::Usage, "empty request", "");
        }

        ControlRequest request;
        std::string error;
        if (!parse_control_request(name, args, request, error)) {
            return encode_control_reply(ControlStatus::Usage, error, "");
        }

        switch (request.command) {
            case ControlCommand::Status:
                return encode_control_reply(ControlStatus::Ok, "",
                                            format_status(this->build_status()));

            case ControlCommand::Devices:
                return encode_control_reply(ControlStatus::Ok, "",
                                            format_device_list(this->bt_.devices()));

            case ControlCommand::Volume: {
                if (this->volume_state_.fixed) {
                    return encode_control_reply(ControlStatus::Usage,
                                                "volume is locked (--fixed-volume)", "");
                }
                const uint8_t vol = request.volume.value_or(DEFAULT_SINK_VOLUME);
                this->volume_state_.apply(this->sink_, &this->state_store_, vol,
                                          this->volume_state_.muted, VolumeSource::SinkDefault,
                                          true);
                this->bt_.set_transport_volume(vol);
                return encode_control_reply(ControlStatus::Ok, "", "");
            }

            case ControlCommand::Mute: {
                if (this->volume_state_.fixed) {
                    return encode_control_reply(ControlStatus::Usage,
                                                "mute is locked (--fixed-volume)", "");
                }
                const bool muted = request.flag.value_or(false);
                this->volume_state_.apply(this->sink_, &this->state_store_,
                                          this->volume_state_.volume, muted,
                                          VolumeSource::SinkDefault, true);
                return encode_control_reply(ControlStatus::Ok, "", "");
            }

            case ControlCommand::Delay: {
                const uint16_t delay = request.delay_ms.value_or(0);
                this->volume_state_.output_delay_ms = delay;
                this->state_store_.set_output_delay_ms(delay);
                log_line(LogLevel::INFO, LOG_TAG, "Output delay set to %u ms locally",
                         static_cast<unsigned>(delay));
                return encode_control_reply(ControlStatus::Ok, "", "");
            }

            case ControlCommand::Play:
            case ControlCommand::Pause:
            case ControlCommand::Stop:
            case ControlCommand::Next:
            case ControlCommand::Previous: {
                AvrcpCommand cmd = AvrcpCommand::Play;
                if (request.command == ControlCommand::Pause) {
                    cmd = AvrcpCommand::Pause;
                } else if (request.command == ControlCommand::Stop) {
                    cmd = AvrcpCommand::Stop;
                } else if (request.command == ControlCommand::Next) {
                    cmd = AvrcpCommand::Next;
                } else if (request.command == ControlCommand::Previous) {
                    cmd = AvrcpCommand::Previous;
                }
                if (!this->bt_.connected_device().has_value()) {
                    return encode_control_reply(ControlStatus::NotConnected,
                                                "no Bluetooth device is connected", "");
                }
                if (!this->bt_.send_media_command(cmd, error)) {
                    return encode_control_reply(ControlStatus::Unsupported, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");
            }

            case ControlCommand::Connect:
                if (!this->bt_.connect_device(request.mac, error)) {
                    return encode_control_reply(ControlStatus::Failed, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");

            case ControlCommand::Disconnect:
                if (!this->bt_.disconnect_device(request.mac, error)) {
                    return encode_control_reply(ControlStatus::NotConnected, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");

            case ControlCommand::Trust:
                if (!this->bt_.set_trusted(request.mac, true, error)) {
                    return encode_control_reply(ControlStatus::Failed, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");

            case ControlCommand::Forget:
                if (!this->bt_.remove_device(request.mac, error)) {
                    return encode_control_reply(ControlStatus::Failed, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");

            case ControlCommand::Discoverable:
                if (!this->bt_.set_discoverable(request.flag.value_or(true), error)) {
                    return encode_control_reply(ControlStatus::Failed, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");

            case ControlCommand::Pair:
                if (!this->bt_.answer_pairing(request.confirm.value_or(false), error)) {
                    return encode_control_reply(ControlStatus::Failed, error, "");
                }
                return encode_control_reply(ControlStatus::Ok, "", "");
        }
        return encode_control_reply(ControlStatus::Failed, "unhandled command", "");
    }

private:
    StatusSnapshot build_status() const {
        StatusSnapshot s;
        s.name = this->opts_.name;
        s.dbus_available = this->bt_.available();
        s.adapter = this->bt_.adapter();
        s.connected_device = this->bt_.connected_device();
        s.discoverable = this->bt_.discoverable();
        const BtPendingPairing pending = this->bt_.pending_pairing();
        s.pairing_active = pending.active;
        s.pairing_device = pending.name.empty()
                               ? pending.address
                               : pending.name + " (" + pending.address + ")";
        s.pairing_passkey = pending.passkey;

        s.streaming = this->bridge_.streaming();
        s.bt_source = this->bridge_.active_source();
        s.format = this->bridge_.stream_format();
        s.media = this->bt_.media();

        s.volume = this->volume_state_.volume;
        s.muted = this->volume_state_.muted;
        s.volume_source = this->volume_state_.source;
        s.output_delay_ms = this->volume_state_.output_delay_ms;
        s.output = this->sink_.name();
        return s;
    }

    const Options& opts_;
    BtManager& bt_;
    BtAudioBridge& bridge_;
    AudioSink& sink_;
    VolumeState& volume_state_;
    StateStore& state_store_;
};

bool start_control_socket(ControlSocket& socket, const Options& opts) {
    if (opts.no_control) {
        log_line(LogLevel::INFO, LOG_TAG_CONTROL,
                 "Not listening on a control socket: --no-control was given");
        return true;
    }
    if (opts.control_socket.empty()) {
        log_line(LogLevel::WARN, LOG_TAG_CONTROL, "No control socket: %s",
                 opts.control_absent_reason.c_str());
        return true;
    }

    std::string error;
    switch (socket.open(opts.control_socket, error)) {
        case ControlSocketStatus::Ok:
            log_line(LogLevel::INFO, LOG_TAG_CONTROL, "Listening on %s",
                     opts.control_socket.c_str());
            return true;
        case ControlSocketStatus::AlreadyRunning:
            log_fatal(LOG_TAG_CONTROL, "%s", error.c_str());
            return false;
        case ControlSocketStatus::Failed:
            break;
    }
    log_line(LogLevel::WARN, LOG_TAG_CONTROL, "%s -- carrying on without a control socket",
             error.c_str());
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    Options opts;
    if (!parse_options(argc, argv, opts)) {
        std::fprintf(stderr, "Try '%s --help' for the full flag list.\n", argv[0]);
        return 1;
    }
    if (opts.show_help) {
        print_usage(stdout, argv[0]);
        return 0;
    }
    if (opts.show_version) {
        print_version(stdout);
        return 0;
    }
    if (opts.list_devices) {
        print_audio_devices(stdout);
        BtAudioBridge::list_sources(stdout);
        return 0;
    }

    if (!opts.subcommand.empty()) {
        ControlRequest request;
        std::string error;
        if (!parse_control_request(opts.subcommand, opts.subcommand_args, request, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return static_cast<int>(ControlStatus::Usage);
        }
        return static_cast<int>(run_control_subcommand(request, opts.control_socket,
                                                       opts.control_absent_reason, stdout));
    }

    if (opts.daemonize && !opts.pidfile.empty()) {
        std::string error;
        if (probe_pidfile(opts.pidfile, error) != PidFileStatus::Ok) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
    }

    if (opts.daemonize && !opts.control_socket.empty()) {
        std::string error;
        if (probe_control_socket(opts.control_socket, error) ==
            ControlSocketStatus::AlreadyRunning) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 1;
        }
    }

    if (!opts.logfile.empty() && !log_to_file(opts.logfile)) {
        return 1;
    }

    set_log_level(opts.log_level);

    if (opts.config_path.empty()) {
        cli_log(LogLevel::INFO,
                "No config file found; every option came from the command line or defaults");
    } else {
        cli_log(LogLevel::INFO, "Config file: %s", opts.config_path.c_str());
    }

    if (opts.daemonize) {
        const bool discard_stderr = opts.logfile.empty();
        std::string error;
        if (!daemonize(discard_stderr, error)) {
            log_fatal(LOG_TAG, "%s", error.c_str());
            return 1;
        }
    }

    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    if (!opts.logfile.empty()) {
        struct sigaction hup = {};
        hup.sa_handler = log_handle_sighup;
        sigemptyset(&hup.sa_mask);
        hup.sa_flags = SA_RESTART;
        sigaction(SIGHUP, &hup, nullptr);
    }

    PidFile pidfile;
    if (!opts.pidfile.empty()) {
        std::string error;
        if (pidfile.acquire(opts.pidfile, error) != PidFileStatus::Ok) {
            log_fatal(LOG_TAG, "%s", error.c_str());
            return 1;
        }
    }

    ControlSocket control_socket;
    if (!start_control_socket(control_socket, opts)) {
        return 1;
    }

    std::string sink_error;
    std::unique_ptr<AudioSink> sink = make_audio_sink(opts.device, opts.buffer_ms, opts.alsa_mixer,
                                                      opts.alsa_mixer_curve, sink_error);
    if (!sink) {
        log_fatal(LOG_TAG_AUDIO, "%s", sink_error.c_str());
        return 1;
    }

    StateStore state_store(state_store_path(opts.state_dir), opts.state_dir.empty());
    if (state_store.path().empty()) {
        log_line(LogLevel::WARN, LOG_TAG,
                 "Nothing set --state-dir, $XDG_STATE_HOME or $HOME; state will not persist");
    } else {
        size_t malformed_line = 0;
        switch (state_store.load(malformed_line)) {
            case StateLoadResult::Loaded:
            case StateLoadResult::Absent:
                log_line(LogLevel::DEBUG, LOG_TAG, "State file: %s", state_store.path().c_str());
                break;
            case StateLoadResult::Corrupt:
                log_line(LogLevel::WARN, LOG_TAG,
                         "%s:%zu is corrupt -- starting fresh and overwriting on next write",
                         state_store.path().c_str(), malformed_line);
                break;
        }
    }

    VolumeState volume_state;
    volume_state.fixed = opts.fixed_volume;
    volume_state.output_delay_ms = state_store.output_delay_ms().value_or(opts.output_delay_ms);

    if (opts.fixed_volume) {
        volume_state.volume = DEFAULT_SINK_VOLUME;
        volume_state.muted = false;
        volume_state.source = VolumeSource::Fixed;
        sink->set_volume(DEFAULT_SINK_VOLUME);
        sink->set_muted(false);
        log_line(LogLevel::INFO, LOG_TAG, "Volume is fixed at 100%%");
    } else {
        bool seeded = false;
#ifdef BT_SPEAKER_CLI_HAVE_ALSA
        if (!opts.alsa_mixer.empty()) {
            bool mixer_muted = false;
            MixerCurve curve = MixerCurve::kSpec;
            parse_mixer_curve(opts.alsa_mixer_curve, curve);
            const int mixer_vol = AlsaAudioSink::read_mixer_volume(opts.alsa_mixer, mixer_muted, curve);
            if (mixer_vol >= 0) {
                volume_state.volume = static_cast<uint8_t>(mixer_vol);
                volume_state.muted = mixer_muted;
                volume_state.source = VolumeSource::Hardware;
                seeded = true;
                log_line(LogLevel::INFO, LOG_TAG, "Volume %u%s from ALSA mixer '%s'",
                         static_cast<unsigned>(volume_state.volume),
                         volume_state.muted ? " (muted)" : "", opts.alsa_mixer.c_str());
            }
        }
#endif
        if (!seeded) {
            const std::optional<uint8_t> rem_vol = state_store.volume();
            const std::optional<bool> rem_muted = state_store.muted();
            if (rem_vol.has_value() || rem_muted.has_value()) {
                volume_state.apply(*sink, nullptr, rem_vol.value_or(DEFAULT_SINK_VOLUME),
                                   rem_muted.value_or(false), VolumeSource::Restored, false);
                log_line(LogLevel::INFO, LOG_TAG, "Restored volume %u%s from %s",
                         static_cast<unsigned>(volume_state.volume),
                         volume_state.muted ? " (muted)" : "", state_store.path().c_str());
            }
        }
    }

    sink->on_local_volume_changed = [&volume_state, &state_store](uint8_t vol, bool muted) {
        if (volume_state.fixed) {
            return;
        }
        volume_state.volume = vol;
        volume_state.muted = muted;
        volume_state.source = VolumeSource::Hardware;
        state_store.set_volume_and_muted(vol, muted);
    };

    BtManagerConfig bt_config;
    bt_config.adapter = opts.adapter;
    bt_config.speaker_name = opts.name;
    bt_config.target_device = opts.target_device;
    bt_config.auto_connect = opts.auto_connect;
    bt_config.discoverable = opts.discoverable;
    bt_config.allow_unpaired = opts.allow_unpaired;
    bt_config.no_dbus = opts.no_dbus;

    BtManager bt_manager(std::move(bt_config));
    bt_manager.on_device_connected = [&state_store](const std::string& mac) {
        state_store.set_last_device(mac);
    };
    bt_manager.on_transport_volume_changed = [&volume_state, &sink, &state_store](uint8_t vol) {
        if (!volume_state.fixed) {
            volume_state.apply(*sink, &state_store, vol, volume_state.muted, VolumeSource::Remote,
                               true);
        }
    };
    bt_manager.start(state_store.last_device());

    BtBridgeConfig bridge_config;
    bridge_config.target_device = opts.target_device;
    bridge_config.release_device = opts.release_device;

    BtAudioBridge bridge(*sink, std::move(bridge_config));

    HookRunner hooks;
    HookContext stream_context;
    if (!opts.hook_start.empty() || !opts.hook_stop.empty()) {
        bridge.on_stream_event = [&opts, &bt_manager, &hooks, &stream_context](
                                     bool started, const std::string& mac) {
            if (started) {
                stream_context = HookContext{};
                stream_context.device_address = mac;
                stream_context.speaker_name = opts.name;
                if (const std::optional<BtAdapterInfo> a = bt_manager.adapter()) {
                    stream_context.adapter = a->name;
                }
                for (const BtDeviceInfo& dev : bt_manager.devices()) {
                    if (dev.address == mac) {
                        stream_context.device_name = dev.name;
                        break;
                    }
                }
            }
            const std::string& cmd = started ? opts.hook_start : opts.hook_stop;
            if (!cmd.empty()) {
                hooks.run(cmd, started ? "start" : "stop", stream_context);
            }
        };
    }
    bridge.start();

    ControlDispatcher dispatcher(opts, bt_manager, bridge, *sink, volume_state, state_store);

    cli_log(LogLevel::INFO, "bt-speaker-cli %s started as \"%s\" (output: %s)",
            BT_SPEAKER_CLI_VERSION, opts.name.c_str(), sink->name().c_str());

    while (g_running.load()) {
        const int64_t now_ms = monotonic_ms();
        bt_manager.poll(now_ms);
        bridge.poll(now_ms);
        control_socket.poll(now_ms, dispatcher);
        sink->poll(now_ms);
        hooks.poll();
        log_reopen_if_requested();
        std::this_thread::sleep_for(std::chrono::milliseconds(LOOP_INTERVAL_MS));
    }

    cli_log(LogLevel::INFO, "Shutting down");
    control_socket.close();
    bridge.stop();
    bt_manager.stop();
    hooks.flush();
    sink->stop();
    return 0;
}
