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

/// AudioSink over ALSA snd_pcm.

#pragma once

#include "audio_sink.h"
#include "pcm_volume.h"
#include "sink_recovery.h"

#include <alsa/asoundlib.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace bt_speaker_cli {

enum class MixerCurve {
    kSpec,
    kCubic,
};

bool parse_mixer_curve(const std::string& name, MixerCurve& curve);

class AlsaAudioSink final : public AudioSink {
public:
    AlsaAudioSink(std::string device, uint32_t buffer_ms, std::string mixer_spec = {},
                  MixerCurve curve = MixerCurve::kSpec);
    ~AlsaAudioSink() override;

    std::string name() const override;
    bool configure(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample) override;
    size_t write(const uint8_t* data, size_t length, uint32_t timeout_ms) override;
    void clear() override;
    void release() override;
    void stop() override;
    void set_volume(uint8_t volume) override;
    void set_muted(bool muted) override;
    void poll(int64_t now_ms) override;

    SinkCapabilities capabilities() const override;

    static bool probe(const std::string& device, std::string& error);
    static bool probe_mixer(const std::string& mixer_spec, std::string& error);
    static int read_mixer_volume(const std::string& mixer_spec, bool& muted,
                                 MixerCurve curve = MixerCurve::kSpec);
    static void list_devices(std::FILE* out);

private:
    bool open_device_(uint32_t sample_rate, uint8_t channels, uint8_t bits_per_sample);
    void close_device_();
    bool recover_(int err);
    bool handle_device_loss_();
    void update_target_multiplier_();

    static snd_mixer_elem_t* open_mixer_(const std::string& mixer_spec, snd_mixer_t** handle);
    static bool playback_dB_range_(snd_mixer_elem_t* elem, long& min, long& max);
    static bool playback_dB_(snd_mixer_elem_t* elem, long& dB);
    static bool playback_switch_(snd_mixer_elem_t* elem);

    bool mixer_open_();
    void mixer_close_();
    void adopt_mixer_level_();
    bool write_mixer_dB_(long dB);
    bool set_mixer_mute_(bool muted);
    bool start_mixer_ramp_(long target_dB);
    bool step_mixer_ramp_();
    void report_mixer_change_();
    void poll_mixer_();
    uint64_t software_gain_() const;

    std::string device_;
    uint32_t buffer_ms_;
    std::string mixer_spec_;
    MixerCurve mixer_curve_{MixerCurve::kSpec};

    std::mutex device_mutex_;
    snd_pcm_t* pcm_{nullptr};
    uint32_t rate_{0};
    uint8_t channels_{0};
    uint8_t bits_{0};
    size_t bytes_per_frame_{0};
    StreamFormat last_format_{};
    std::vector<uint8_t> scaled_;

    std::atomic<bool> stopping_{false};
    std::atomic<bool> failed_{false};

    SinkRecovery recovery_;

    std::atomic<uint8_t> volume_{DEFAULT_SINK_VOLUME};
    std::atomic<bool> muted_{false};
    std::atomic<uint64_t> target_multiplier_{Q32_ONE};
    std::uint64_t current_multiplier_{Q32_ONE};

    snd_mixer_t* mixer_{nullptr};
    snd_mixer_elem_t* mixer_elem_{nullptr};
    long mixer_dB_min_{0};
    long mixer_dB_max_{0};
    bool mixer_has_switch_{false};
    bool mixer_carries_volume_{false};
    bool mixer_carries_mute_{false};
    bool mixer_reported_muted_{false};
    long mixer_reported_dB_{0};
    bool mixer_ramping_{false};
    long mixer_ramp_from_dB_{0};
    long mixer_ramp_target_dB_{0};
    int mixer_ramp_step_{0};
    int64_t mixer_ramp_next_ms_{0};
};

long mixer_dB_for_volume(long min, long max, uint8_t volume, MixerCurve curve = MixerCurve::kSpec);
uint8_t volume_for_mixer_dB(long min, long max, long dB, MixerCurve curve = MixerCurve::kSpec);
void install_alsa_error_handler();
void list_alsa_pcms(std::FILE* out, snd_pcm_stream_t stream,
                    bool (*reachable)(const std::string& pcm));

}  // namespace bt_speaker_cli
