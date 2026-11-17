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

/// AudioSink over libpipewire.

#pragma once

#include "audio_sink.h"
#include "pcm_ring.h"
#include "pcm_volume.h"
#include "sink_recovery.h"

#include <pipewire/pipewire.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

namespace bt_speaker_cli {

inline constexpr int PIPEWIRE_TIMEOUT_S = 3;
inline constexpr int PIPEWIRE_STREAM_TIMEOUT_MS = PIPEWIRE_TIMEOUT_S * 1000;
inline constexpr int PIPEWIRE_RECOVERY_TIMEOUT_MS = 500;
inline constexpr size_t RING_QUANTUM_MULTIPLE = 3;
inline constexpr size_t MIN_RING_FRAMES = 1024;

size_t pipewire_ring_frames(uint32_t rate, uint32_t buffer_ms);

struct PipeWireQuantumFit {
    bool starves{false};
    bool tight{false};
    uint32_t recommended_buffer_ms{0};
};

PipeWireQuantumFit pipewire_quantum_fit(size_t ring_frames, uint32_t quantum, uint32_t rate);

class PipeWireGuard {
public:
    PipeWireGuard();
    ~PipeWireGuard();

    PipeWireGuard(const PipeWireGuard&) = delete;
    PipeWireGuard& operator=(const PipeWireGuard&) = delete;
};

class LoopLock {
public:
    explicit LoopLock(pw_thread_loop* loop) : loop_(loop) {
        pw_thread_loop_lock(this->loop_);
    }
    ~LoopLock() {
        pw_thread_loop_unlock(this->loop_);
    }

    LoopLock(const LoopLock&) = delete;
    LoopLock& operator=(const LoopLock&) = delete;

private:
    pw_thread_loop* loop_;
};

struct PipeWireNode {
    std::string name;
    std::string description;
    std::string media_class;
};

bool pipewire_list_nodes(std::initializer_list<const char*> media_classes,
                         std::vector<PipeWireNode>& out, std::string& error,
                         int timeout_ms = PIPEWIRE_TIMEOUT_S * 1000);

class PipeWireSink final : public AudioSink {
public:
    PipeWireSink(std::string device, uint32_t buffer_ms);
    ~PipeWireSink() override;

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
    static void stream_state_cb(void* userdata, enum pw_stream_state old,
                                enum pw_stream_state state, const char* error);
    static void stream_process_cb(void* userdata);

    bool open_stream_(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample,
                      int timeout_ms);
    void close_stream_();
    bool start_loop_();
    void stop_loop_();
    bool reopen_in_place_();
    void discard_ring_tail_();
    bool stream_alive_() const;
    size_t ring_capacity_() const;
    const uint8_t* stage_(const uint8_t* data, size_t frames, uint64_t start, uint64_t target);
    void update_target_multiplier_();

    PipeWireGuard pw_;

    std::string device_;
    uint32_t buffer_ms_;

    std::mutex mutex_;
    std::condition_variable space_available_;
    PcmRingBuffer ring_;

    pw_thread_loop* loop_{nullptr};
    pw_stream* stream_{nullptr};

    uint32_t rate_{0};
    uint8_t channels_{0};
    uint8_t bits_{0};
    size_t bytes_per_frame_{0};
    uint64_t ramp_step_{0};
    std::vector<uint8_t> scratch_;
    std::atomic<uint32_t> quantum_frames_{0};
    std::atomic<bool> quantum_logged_{false};

    uint64_t stream_generation_{0};

    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> stream_failed_{false};
    std::atomic<int> stream_state_{PW_STREAM_STATE_UNCONNECTED};

    StreamFormat last_format_{};
    SinkRecovery recovery_;
    OutageGapHandoff gap_handoff_;

    std::atomic<uint8_t> volume_{DEFAULT_SINK_VOLUME};
    std::atomic<bool> muted_{false};
    std::atomic<uint64_t> target_multiplier_{Q32_ONE};
    uint64_t current_multiplier_{Q32_ONE};
};

}  // namespace bt_speaker_cli
