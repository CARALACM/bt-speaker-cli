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

/// Bridges Bluetooth A2DP PulseAudio sources into an AudioSink.

#pragma once

#include "audio_sink.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace bt_speaker_cli {

struct BtBridgeConfig {
    std::string target_device;  ///< normalized MAC filter, or empty for any BT A2DP source
    bool release_device{false};
    bool unload_duplicate_loopback{true};
    bool disabled{false};  ///< true when built without libpulse or output is not bridged
};

class BtAudioBridge {
public:
    BtAudioBridge(AudioSink& sink, BtBridgeConfig config);
    ~BtAudioBridge();

    BtAudioBridge(const BtAudioBridge&) = delete;
    BtAudioBridge& operator=(const BtAudioBridge&) = delete;

    void start();
    void poll(int64_t now_ms);
    void stop();

    bool streaming() const;
    std::string active_source() const;
    std::string active_device_mac() const;
    std::optional<StreamFormat> stream_format() const;

    /// Fired on the main loop when A2DP audio bridging starts (true) or stops (false).
    std::function<void(bool started, const std::string& mac)> on_stream_event;

    /// Prints detected Bluetooth A2DP sources in PulseAudio, for -l.
    static void list_sources(std::FILE* out);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bt_speaker_cli
