// Copyright 2026 CARALACM
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

#include "bt_audio_bridge.h"

#include "bt_reconnect.h"
#include "log.h"

#ifdef BT_SPEAKER_CLI_HAVE_PULSE
#include "pulse_sink.h"
#endif

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bt_speaker_cli {

static constexpr const char* LOG_TAG = LOG_TAG_BRIDGE;

#ifdef BT_SPEAKER_CLI_HAVE_PULSE

namespace {

constexpr const char* BRIDGE_STREAM_NAME = "bt-speaker-cli capture";
constexpr pa_usec_t CAPTURE_FRAG_US = 20000;
constexpr int64_t PULSE_RESCAN_INTERVAL_MS = 2000;
constexpr int64_t PULSE_RETRY_INITIAL_MS = 2000;
constexpr int64_t PULSE_RETRY_MAX_MS = 10000;
constexpr uint32_t CAPTURE_READ_TIMEOUT_MS = 100;
constexpr uint32_t SINK_WRITE_TIMEOUT_MS = 100;

struct BtPulseSourceInfo {
    std::string name;
    std::string description;
    std::string mac;
    pa_sample_spec spec{};
    pa_source_state_t state{PA_SOURCE_INVALID_STATE};
};

struct SourceListQuery : PulseQuery {
    std::vector<BtPulseSourceInfo> sources;
};

struct ModuleInfoEntry {
    uint32_t index{PA_INVALID_INDEX};
    std::string name;
    std::string argument;
};

struct ModuleListQuery : PulseQuery {
    std::vector<ModuleInfoEntry> modules;
};

void bt_source_info_cb(pa_context* /*context*/, const pa_source_info* info, int eol,
                       void* userdata) {
    auto* query = static_cast<SourceListQuery*>(userdata);
    if (eol < 0) {
        query->finish(false);
        return;
    }
    if (eol > 0) {
        query->finish(true);
        return;
    }
    if (info == nullptr || info->name == nullptr) {
        return;
    }
    const std::optional<std::string> mac = mac_from_pulse_bt_source(info->name);
    if (!mac.has_value()) {
        return;
    }
    BtPulseSourceInfo entry;
    entry.name = info->name;
    entry.description = info->description != nullptr ? info->description : "";
    entry.mac = *mac;
    entry.spec = info->sample_spec;
    entry.state = info->state;
    query->sources.push_back(std::move(entry));
}

void module_info_cb(pa_context* /*context*/, const pa_module_info* info, int eol, void* userdata) {
    auto* query = static_cast<ModuleListQuery*>(userdata);
    if (eol < 0) {
        query->finish(false);
        return;
    }
    if (eol > 0) {
        query->finish(true);
        return;
    }
    if (info == nullptr || info->name == nullptr) {
        return;
    }
    ModuleInfoEntry entry;
    entry.index = info->index;
    entry.name = info->name;
    entry.argument = info->argument != nullptr ? info->argument : "";
    query->modules.push_back(std::move(entry));
}

bool list_bt_sources(PulseConnection& conn, std::vector<BtPulseSourceInfo>& out) {
    SourceListQuery query;
    query.conn = &conn;
    pa_operation* op = nullptr;
    {
        const MainloopLock lock(conn.mainloop());
        op = pa_context_get_source_info_list(conn.context(), bt_source_info_cb, &query);
    }
    if (!await_query(conn, query, op, PULSE_RECOVERY_TIMEOUT_MS)) {
        return false;
    }
    out = std::move(query.sources);
    return true;
}

void unload_duplicate_loopbacks(PulseConnection& conn, const std::string& source_name) {
    ModuleListQuery query;
    query.conn = &conn;
    pa_operation* op = nullptr;
    {
        const MainloopLock lock(conn.mainloop());
        op = pa_context_get_module_info_list(conn.context(), module_info_cb, &query);
    }
    if (!await_query(conn, query, op, PULSE_RECOVERY_TIMEOUT_MS)) {
        return;
    }
    for (const ModuleInfoEntry& mod : query.modules) {
        if (mod.name == "module-loopback" && mod.argument.find(source_name) != std::string::npos) {
            cli_log(LogLevel::INFO,
                    "Unloading PulseAudio module-loopback #%u on '%s' to prevent double audio",
                    mod.index, source_name.c_str());
            const MainloopLock lock(conn.mainloop());
            pa_operation* unload_op =
                pa_context_unload_module(conn.context(), mod.index, nullptr, nullptr);
            if (unload_op != nullptr) {
                pa_operation_unref(unload_op);
            }
        }
    }
}

StreamFormat capture_format_for(const pa_sample_spec& spec) {
    StreamFormat fmt;
    fmt.sample_rate = (spec.rate >= 8000 && spec.rate <= 192000) ? spec.rate : 48000;
    fmt.channels = (spec.channels >= 1 && spec.channels <= 8) ? spec.channels : 2;
    switch (spec.format) {
        case PA_SAMPLE_S24LE:
            fmt.bit_depth = 24;
            break;
        case PA_SAMPLE_S32LE:
            fmt.bit_depth = 32;
            break;
        default:
            fmt.bit_depth = 16;
            break;
    }
    return fmt;
}

pa_sample_format_t pulse_capture_format(uint8_t bit_depth) {
    switch (bit_depth) {
        case 24:
            return PA_SAMPLE_S24LE;
        case 32:
            return PA_SAMPLE_S32LE;
        default:
            return PA_SAMPLE_S16LE;
    }
}

}  // namespace

struct BtAudioBridge::Impl {
    Impl(AudioSink& s, BtBridgeConfig cfg) : sink(s), config(std::move(cfg)) {}

    ~Impl() {
        this->stop_capture();
        this->monitor_conn.disconnect();
    }

    AudioSink& sink;
    BtBridgeConfig config;
    std::function<void(bool started, const std::string& mac)> on_stream_event;

    PulseConnection monitor_conn;
    bool subscribed{false};
    bool pulse_unavailable_logged{false};
    int64_t retry_delay_ms{PULSE_RETRY_INITIAL_MS};
    int64_t next_connect_ms{0};
    int64_t next_scan_ms{0};
    std::atomic<bool> rescan_needed{true};

    // Active capture state
    std::thread capture_thread;
    std::atomic<bool> stop_capture_requested{false};
    std::atomic<bool> capture_finished{false};
    std::atomic<bool> streaming{false};
    std::string active_source;
    std::string active_mac;
    std::optional<StreamFormat> active_format;

    static void subscribe_cb(pa_context* /*c*/, pa_subscription_event_type_t t, uint32_t /*idx*/,
                             void* userdata) {
        auto* self = static_cast<Impl*>(userdata);
        const auto facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
        if (facility == PA_SUBSCRIPTION_EVENT_SOURCE || facility == PA_SUBSCRIPTION_EVENT_MODULE) {
            self->rescan_needed.store(true, std::memory_order_relaxed);
        }
    }

    bool ensure_monitor_connected(int64_t now_ms) {
        if (this->monitor_conn.ready()) {
            return true;
        }
        this->subscribed = false;
        if (now_ms < this->next_connect_ms) {
            return false;
        }
        std::string error;
        if (!this->monitor_conn.connect(error, PULSE_RECOVERY_TIMEOUT_MS)) {
            if (!this->pulse_unavailable_logged) {
                cli_log(LogLevel::WARN, "%s -- retrying in background", error.c_str());
                this->pulse_unavailable_logged = true;
            }
            this->next_connect_ms = now_ms + this->retry_delay_ms;
            this->retry_delay_ms = std::min(this->retry_delay_ms * 2, PULSE_RETRY_MAX_MS);
            return false;
        }

        this->pulse_unavailable_logged = false;
        this->retry_delay_ms = PULSE_RETRY_INITIAL_MS;
        {
            const MainloopLock ml(this->monitor_conn.mainloop());
            pa_context_set_subscribe_callback(this->monitor_conn.context(), &Impl::subscribe_cb,
                                              this);
            pa_operation* op = pa_context_subscribe(
                this->monitor_conn.context(),
                static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SOURCE |
                                                    PA_SUBSCRIPTION_MASK_MODULE),
                nullptr, nullptr);
            if (op != nullptr) {
                pa_operation_unref(op);
            }
        }
        this->subscribed = true;
        this->rescan_needed.store(true, std::memory_order_relaxed);
        cli_log(LogLevel::INFO, "Connected to PulseAudio %s for Bluetooth source monitoring",
                this->monitor_conn.server_name().c_str());
        return true;
    }

    void start_capture(const BtPulseSourceInfo& src) {
        this->stop_capture();

        if (this->config.unload_duplicate_loopback) {
            unload_duplicate_loopbacks(this->monitor_conn, src.name);
        }

        const StreamFormat fmt = capture_format_for(src.spec);
        if (!this->sink.configure(fmt.sample_rate, fmt.channels, fmt.bit_depth)) {
            cli_log(LogLevel::ERROR, "Output sink '%s' refused %u Hz / %u ch / %u-bit",
                    this->sink.name().c_str(), fmt.sample_rate, fmt.channels, fmt.bit_depth);
            return;
        }

        this->active_source = src.name;
        this->active_mac = src.mac;
        this->active_format = fmt;
        this->stop_capture_requested.store(false);
        this->capture_finished.store(false);
        this->streaming.store(true);

        cli_log(LogLevel::INFO, "Bridging Bluetooth source '%s' (%u Hz / %u ch / %u-bit) -> '%s'",
                src.name.c_str(), fmt.sample_rate, fmt.channels, fmt.bit_depth,
                this->sink.name().c_str());

        if (this->on_stream_event) {
            this->on_stream_event(true, src.mac);
        }

        this->capture_thread = std::thread([this, source_name = src.name, fmt]() {
            this->run_capture_thread(source_name, fmt);
        });
    }

    void stop_capture() {
        this->stop_capture_requested.store(true);
        if (this->capture_thread.joinable()) {
            this->capture_thread.join();
        }
        if (this->streaming.exchange(false)) {
            const std::string ended_mac = this->active_mac;
            this->active_source.clear();
            this->active_mac.clear();
            this->active_format.reset();
            if (this->config.release_device) {
                this->sink.release();
            } else {
                this->sink.clear();
            }
            cli_log(LogLevel::INFO, "Bluetooth audio stream ended");
            if (this->on_stream_event) {
                this->on_stream_event(false, ended_mac);
            }
        }
    }

    void run_capture_thread(const std::string& source_name, const StreamFormat& fmt) {
        PulseConnection cap_conn;
        std::string error;
        if (!cap_conn.connect(error, PULSE_TIMEOUT_MS)) {
            cli_log(LogLevel::ERROR, "Capture connection failed: %s", error.c_str());
            this->capture_finished.store(true);
            return;
        }

        pa_sample_spec spec{};
        spec.format = pulse_capture_format(fmt.bit_depth);
        spec.rate = fmt.sample_rate;
        spec.channels = fmt.channels;
        const size_t bytes_per_frame = pa_frame_size(&spec);

        pa_buffer_attr attr{};
        attr.maxlength = static_cast<uint32_t>(-1);
        attr.tlength = static_cast<uint32_t>(-1);
        attr.prebuf = static_cast<uint32_t>(-1);
        attr.minreq = static_cast<uint32_t>(-1);
        attr.fragsize = static_cast<uint32_t>(pa_usec_to_bytes(CAPTURE_FRAG_US, &spec));

        std::atomic<bool> readable{false};
        std::atomic<bool> lost{false};
        std::atomic<int> stream_state{PA_STREAM_UNCONNECTED};

        struct CaptureCallbacks {
            PulseConnection* conn;
            std::atomic<bool>* readable;
            std::atomic<bool>* lost;
            std::atomic<int>* state;
        } cb_ctx{&cap_conn, &readable, &lost, &stream_state};

        auto state_cb = [](pa_stream* s, void* ud) {
            auto* ctx = static_cast<CaptureCallbacks*>(ud);
            const pa_stream_state_t st = pa_stream_get_state(s);
            ctx->state->store(static_cast<int>(st));
            if (st == PA_STREAM_FAILED || st == PA_STREAM_TERMINATED) {
                ctx->lost->store(true);
            }
            ctx->conn->notify();
        };
        auto read_cb = [](pa_stream* /*s*/, size_t /*nbytes*/, void* ud) {
            auto* ctx = static_cast<CaptureCallbacks*>(ud);
            ctx->readable->store(true);
            ctx->conn->notify();
        };

        pa_stream* stream = nullptr;
        int err = -1;
        {
            const MainloopLock ml(cap_conn.mainloop());
            stream = pa_stream_new(cap_conn.context(), BRIDGE_STREAM_NAME, &spec, nullptr);
            if (stream != nullptr) {
                pa_stream_set_state_callback(stream, state_cb, &cb_ctx);
                pa_stream_set_read_callback(stream, read_cb, &cb_ctx);
                const auto flags = static_cast<pa_stream_flags_t>(
                    PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE);
                err = pa_stream_connect_record(stream, source_name.c_str(), &attr, flags);
            }
        }
        if (err < 0 || stream == nullptr) {
            cli_log(LogLevel::ERROR, "Cannot open PulseAudio capture on '%s'", source_name.c_str());
            if (stream != nullptr) {
                const MainloopLock ml(cap_conn.mainloop());
                pa_stream_set_state_callback(stream, nullptr, nullptr);
                pa_stream_set_read_callback(stream, nullptr, nullptr);
                pa_stream_disconnect(stream);
                pa_stream_unref(stream);
            }
            this->capture_finished.store(true);
            return;
        }

        const bool settled = cap_conn.wait_for(
            [&stream_state] {
                const auto st = static_cast<pa_stream_state_t>(stream_state.load());
                return st == PA_STREAM_READY || PA_STREAM_IS_GOOD(st) == 0;
            },
            PULSE_TIMEOUT_MS);

        if (!settled || stream_state.load() != PA_STREAM_READY) {
            cli_log(LogLevel::ERROR, "PulseAudio capture stream '%s' did not become ready",
                    source_name.c_str());
            const MainloopLock ml(cap_conn.mainloop());
            pa_stream_set_state_callback(stream, nullptr, nullptr);
            pa_stream_set_read_callback(stream, nullptr, nullptr);
            pa_stream_disconnect(stream);
            pa_stream_unref(stream);
            this->capture_finished.store(true);
            return;
        }

        std::vector<uint8_t> pcm_buf;
        while (!this->stop_capture_requested.load() && !lost.load() && cap_conn.ready()) {
            readable.store(false);
            pcm_buf.clear();
            bool stream_broken = false;
            {
                const MainloopLock ml(cap_conn.mainloop());
                while (pcm_buf.size() < 32768) {
                    const void* frag = nullptr;
                    size_t size = 0;
                    if (pa_stream_peek(stream, &frag, &size) < 0) {
                        stream_broken = true;
                        break;
                    }
                    if (size == 0) {
                        break;
                    }
                    if (frag != nullptr) {
                        const auto* bytes = static_cast<const uint8_t*>(frag);
                        pcm_buf.insert(pcm_buf.end(), bytes, bytes + size);
                    }
                    pa_stream_drop(stream);
                }
            }

            if (stream_broken) {
                break;
            }

            if (!pcm_buf.empty()) {
                const size_t aligned = pcm_buf.size() - (pcm_buf.size() % bytes_per_frame);
                size_t offset = 0;
                while (offset < aligned && !this->stop_capture_requested.load() && !lost.load()) {
                    const size_t wrote = this->sink.write(pcm_buf.data() + offset,
                                                          aligned - offset, SINK_WRITE_TIMEOUT_MS);
                    if (wrote == 0) {
                        break;
                    }
                    offset += wrote;
                }
                continue;
            }

            cap_conn.wait_for(
                [&] {
                    return readable.load() || lost.load() ||
                           this->stop_capture_requested.load() || !cap_conn.ready();
                },
                static_cast<int>(CAPTURE_READ_TIMEOUT_MS));
        }

        {
            const MainloopLock ml(cap_conn.mainloop());
            pa_stream_set_state_callback(stream, nullptr, nullptr);
            pa_stream_set_read_callback(stream, nullptr, nullptr);
            pa_stream_disconnect(stream);
            pa_stream_unref(stream);
        }
        this->capture_finished.store(true);
    }
};

#else

struct BtAudioBridge::Impl {
    Impl(AudioSink& s, BtBridgeConfig cfg) : sink(s), config(std::move(cfg)) {}
    AudioSink& sink;
    BtBridgeConfig config;
    std::function<void(bool started, const std::string& mac)> on_stream_event;
    std::atomic<bool> streaming{false};
    std::string active_source;
    std::string active_mac;
    std::optional<StreamFormat> active_format;
};

#endif

BtAudioBridge::BtAudioBridge(AudioSink& sink, BtBridgeConfig config)
    : impl_(std::make_unique<Impl>(sink, std::move(config))) {}

BtAudioBridge::~BtAudioBridge() {
    this->stop();
}

void BtAudioBridge::start() {
    this->impl_->on_stream_event = this->on_stream_event;
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    if (this->impl_->config.disabled) {
        return;
    }
    this->impl_->rescan_needed.store(true, std::memory_order_relaxed);
#endif
}

void BtAudioBridge::poll(int64_t now_ms) {
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    if (this->impl_->config.disabled) {
        return;
    }

    if (this->impl_->streaming.load() && this->impl_->capture_finished.load()) {
        this->impl_->stop_capture();
        this->impl_->rescan_needed.store(true, std::memory_order_relaxed);
    }

    if (!this->impl_->ensure_monitor_connected(now_ms)) {
        return;
    }

    const bool due =
        this->impl_->rescan_needed.exchange(false, std::memory_order_relaxed) ||
        now_ms >= this->impl_->next_scan_ms;
    if (!due) {
        return;
    }
    this->impl_->next_scan_ms = now_ms + PULSE_RESCAN_INTERVAL_MS;

    std::vector<BtPulseSourceInfo> sources;
    if (!list_bt_sources(this->impl_->monitor_conn, sources)) {
        return;
    }

    const BtPulseSourceInfo* chosen = nullptr;
    for (const BtPulseSourceInfo& src : sources) {
        if (is_matching_pulse_bt_source(src.name, this->impl_->config.target_device)) {
            chosen = &src;
            break;
        }
    }

    if (this->impl_->streaming.load()) {
        const bool still_present =
            chosen != nullptr && chosen->name == this->impl_->active_source;
        if (!still_present) {
            this->impl_->stop_capture();
        }
        return;
    }

    if (chosen != nullptr) {
        this->impl_->start_capture(*chosen);
    }
#else
    static_cast<void>(now_ms);
#endif
}

void BtAudioBridge::stop() {
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    this->impl_->stop_capture();
    this->impl_->monitor_conn.disconnect();
#endif
}

bool BtAudioBridge::streaming() const {
    return this->impl_->streaming.load();
}

std::string BtAudioBridge::active_source() const {
    return this->impl_->active_source;
}

std::string BtAudioBridge::active_device_mac() const {
    return this->impl_->active_mac;
}

std::optional<StreamFormat> BtAudioBridge::stream_format() const {
    return this->impl_->active_format;
}

void BtAudioBridge::list_sources(std::FILE* out) {
    std::fprintf(out, "\nBluetooth audio sources in PulseAudio (auto-bridged when connected):\n");
#ifdef BT_SPEAKER_CLI_HAVE_PULSE
    PulseConnection conn;
    std::string error;
    if (!conn.connect(error)) {
        std::fprintf(out, "  (%s)\n", error.c_str());
        return;
    }
    std::vector<BtPulseSourceInfo> sources;
    if (!list_bt_sources(conn, sources)) {
        std::fprintf(out, "  (cannot list PulseAudio sources)\n");
        return;
    }
    if (sources.empty()) {
        std::fprintf(out, "  (no active Bluetooth A2DP sources right now -- connect a PC first)\n");
        return;
    }
    for (const BtPulseSourceInfo& src : sources) {
        std::fprintf(out, "  %s  [%s]\n", src.name.c_str(), src.mac.c_str());
        if (!src.description.empty()) {
            std::fprintf(out, "      %s\n", src.description.c_str());
        }
        char spec[PA_SAMPLE_SPEC_SNPRINT_MAX];
        pa_sample_spec_snprint(spec, sizeof(spec), &src.spec);
        std::fprintf(out, "      running at %s\n", spec);
    }
#else
    std::fprintf(out, "  (built without libpulse)\n");
#endif
}

}  // namespace bt_speaker_cli
