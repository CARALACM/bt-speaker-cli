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

/// Backend-agnostic destination for captured Bluetooth PCM.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bt_speaker_cli {

inline constexpr std::array<uint32_t, 8> PROBE_RATES{22050, 32000, 44100,  48000,
                                                     88200, 96000, 176400, 192000};
inline constexpr std::array<uint8_t, 4> PROBE_BIT_DEPTHS{8, 16, 24, 32};
inline constexpr std::array<uint8_t, 5> PROBE_CHANNELS{1, 2, 4, 6, 8};

struct SinkCapabilities {
    std::vector<uint32_t> rates;
    std::vector<uint8_t> bit_depths;
    std::vector<uint8_t> channels;

    static SinkCapabilities permissive();
};

inline constexpr uint8_t DEFAULT_SINK_VOLUME = 100;

enum class VolumeSource {
    SinkDefault,
    Restored,
    Remote,
    Fixed,
    Hardware,
};

struct StreamFormat {
    uint32_t sample_rate{0};
    uint8_t channels{0};
    uint8_t bit_depth{0};
};

class AudioSink {
public:
    virtual ~AudioSink() = default;

    AudioSink(const AudioSink&) = delete;
    AudioSink& operator=(const AudioSink&) = delete;

    virtual std::string name() const = 0;
    virtual bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample) = 0;
    virtual size_t write(const uint8_t* data, size_t length, uint32_t timeout_ms) = 0;
    virtual void clear() {}
    virtual void release() {
        this->clear();
    }
    virtual void stop() {}
    virtual void set_volume(uint8_t /*volume*/) {}
    virtual void set_muted(bool /*muted*/) {}
    virtual void poll(int64_t /*now_ms*/) {}

    virtual SinkCapabilities capabilities() const {
        return SinkCapabilities::permissive();
    }

    std::function<void(uint32_t frames, int64_t timestamp)> on_frames_played;
    std::function<void(uint8_t volume, bool muted)> on_local_volume_changed;

protected:
    AudioSink() = default;
};

enum class SinkBackend {
    Null,
    Stdout,
    Alsa,
    Pulse,
    PipeWire,
};

struct DeviceSpec {
    SinkBackend backend{SinkBackend::Null};
    std::string device;
};

bool resolve_device_spec(const std::string& spec, DeviceSpec& out, std::string& error);
std::string audio_backend_list();
bool alsa_pcm_is_reachable(const std::string& pcm);

void print_sink_capabilities(std::FILE* out, const SinkCapabilities& caps,
                             const std::array<const char*, PROBE_BIT_DEPTHS.size()>& depth_names);

/// Builds the sink named by -o; PulseAudio and PipeWire tolerate a server that is still starting.
std::unique_ptr<AudioSink> make_audio_sink(const std::string& device, uint32_t buffer_ms,
                                           const std::string& alsa_mixer,
                                           const std::string& alsa_mixer_curve, std::string& error);

void print_audio_devices(std::FILE* out);

}  // namespace bt_speaker_cli
