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

/// AudioSink over libpulse, sharing PulseConnection helpers with BtAudioBridge.

#pragma once

#include "audio_sink.h"
#include "pcm_volume.h"
#include "sink_recovery.h"

#include <pulse/pulseaudio.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace bt_speaker_cli {

inline constexpr int PULSE_TIMEOUT_MS = 3000;
inline constexpr int PULSE_RECOVERY_TIMEOUT_MS = 500;

/// A libpulse threaded mainloop and its context, as one lifetime.
class PulseConnection {
public:
    PulseConnection() = default;
    ~PulseConnection();

    PulseConnection(const PulseConnection&) = delete;
    PulseConnection& operator=(const PulseConnection&) = delete;

    bool connect(std::string& error, int timeout_ms = PULSE_TIMEOUT_MS);
    void disconnect();
    bool ready() const;
    std::string server_name() const;

    pa_threaded_mainloop* mainloop() const {
        return this->loop_;
    }
    pa_context* context() const {
        return this->context_;
    }

    template <typename Predicate>
    bool wait_for(Predicate predicate, int timeout_ms = PULSE_TIMEOUT_MS) {
        std::unique_lock<std::mutex> lock(this->wait_mutex_);
        return this->wait_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), predicate);
    }

    void notify();

private:
    static void state_cb(pa_context* context, void* userdata);

    pa_threaded_mainloop* loop_{nullptr};
    pa_context* context_{nullptr};
    std::atomic<pa_context_state_t> state_{PA_CONTEXT_UNCONNECTED};

    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
};

class MainloopLock {
public:
    explicit MainloopLock(pa_threaded_mainloop* loop) : loop_(loop) {
        pa_threaded_mainloop_lock(this->loop_);
    }
    ~MainloopLock() {
        pa_threaded_mainloop_unlock(this->loop_);
    }

    MainloopLock(const MainloopLock&) = delete;
    MainloopLock& operator=(const MainloopLock&) = delete;

private:
    pa_threaded_mainloop* loop_;
};

struct PulseQuery {
    PulseConnection* conn{nullptr};
    std::atomic<bool> done{false};
    bool failed{false};

    void finish(bool ok) {
        this->failed = !ok;
        this->done.store(true, std::memory_order_release);
        this->conn->notify();
    }
};

struct PulseSinkInfo {
    std::string name;
    std::string description;
    std::string monitor_source;
    pa_sample_spec spec{};
};

bool list_sinks(PulseConnection& conn, std::vector<PulseSinkInfo>& out,
                int timeout_ms = PULSE_TIMEOUT_MS);
std::string default_sink_name(PulseConnection& conn, int timeout_ms = PULSE_TIMEOUT_MS);
bool await_query(PulseConnection& conn, PulseQuery& query, pa_operation* op,
                 int timeout_ms = PULSE_TIMEOUT_MS);

class PulseAudioSink final : public AudioSink {
public:
    PulseAudioSink(std::string device, uint32_t buffer_ms);
    ~PulseAudioSink() override;

    std::string name() const override;
    bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample) override;
    size_t write(const uint8_t* data, size_t length, uint32_t timeout_ms) override;
    void clear() override;
    void stop() override;
    void set_volume(uint8_t volume) override;
    void set_muted(bool muted) override;
    void poll(int64_t now_ms) override;

    SinkCapabilities capabilities() const override;

    static bool probe(const std::string& device, std::string& error);
    static void list_devices(std::FILE* out);

private:
    static void stream_state_cb(pa_stream* stream, void* userdata);
    static void stream_write_cb(pa_stream* stream, size_t nbytes, void* userdata);
    static void stream_underflow_cb(pa_stream* stream, void* userdata);

    bool open_stream_(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample,
                      int timeout_ms);
    void close_stream_();
    bool reopen_in_place_();
    void report_failed_recovery_(const std::string& reason);
    bool stream_alive_() const;
    const uint8_t* stage_(const uint8_t* data, size_t frames, uint64_t start, uint64_t target);
    void update_target_multiplier_();

    PulseConnection conn_;

    std::string device_;
    uint32_t buffer_ms_;

    std::mutex mutex_;
    std::condition_variable space_available_;
    pa_stream* stream_{nullptr};
    uint32_t rate_{0};
    uint8_t channels_{0};
    uint8_t bits_{0};
    size_t bytes_per_frame_{0};
    uint64_t ramp_step_{0};
    std::vector<uint8_t> scratch_;

    uint64_t stream_generation_{0};

    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> stream_failed_{false};
    std::atomic<int> stream_state_{PA_STREAM_UNCONNECTED};

    StreamFormat last_format_{};
    SinkRecovery recovery_;

    std::atomic<uint8_t> volume_{DEFAULT_SINK_VOLUME};
    std::atomic<bool> muted_{false};
    std::atomic<uint64_t> target_multiplier_{Q32_ONE};
    uint64_t current_multiplier_{Q32_ONE};
};

}  // namespace bt_speaker_cli
