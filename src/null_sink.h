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

/// Device-less AudioSink: discards PCM, or forwards it raw to stdout.

#pragma once

#include "audio_sink.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bt_speaker_cli {

enum class NullSinkOutput {
    Discard,
    Stdout,
};

class NullAudioSink final : public AudioSink {
public:
    explicit NullAudioSink(NullSinkOutput output);

    std::string name() const override;
    bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample) override;
    size_t write(const uint8_t* data, size_t length, uint32_t timeout_ms) override;
    void clear() override;
    void stop() override;
    void set_volume(uint8_t volume) override;
    void set_muted(bool muted) override;

    size_t total_bytes() const;

private:
    void report_played(size_t bytes);

    NullSinkOutput output_;
    std::atomic<size_t> total_bytes_{0};
    std::atomic<size_t> bytes_per_frame_{0};
    std::atomic<uint32_t> sample_rate_{0};
    int64_t playhead_us_{0};
    std::atomic<bool> stdout_failed_{false};
    std::atomic<uint8_t> volume_{DEFAULT_SINK_VOLUME};
    std::atomic<bool> muted_{false};
};

}  // namespace bt_speaker_cli
