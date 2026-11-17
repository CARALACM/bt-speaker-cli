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

/// Bluetooth MAC normalization, PulseAudio source matching, and reconnection backoff.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bt_speaker_cli {

inline constexpr int64_t BT_RECONNECT_INITIAL_MS = 2000;
inline constexpr int64_t BT_RECONNECT_MAX_MS = 30000;

/// Known Bluetooth remote device tracked from BlueZ.
struct BtDeviceInfo {
    std::string object_path;  ///< e.g. /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
    std::string address;      ///< normalized uppercase XX:XX:XX:XX:XX:XX
    std::string name;
    bool paired{false};
    bool trusted{false};
    bool connected{false};
    int16_t rssi{0};
    bool has_audio_source{false};
};

/// Active A2DP/AVRCP media state from BlueZ MediaPlayer1 / MediaTransport1.
struct BtMediaInfo {
    std::string status;  ///< "playing", "paused", "stopped", or empty
    std::string title;
    std::string artist;
    std::string album;
    std::optional<uint32_t> position_ms{};
    std::optional<uint32_t> duration_ms{};
    std::optional<uint8_t> transport_volume{};  ///< 0-100 mapped from A2DP 0-127
    bool transport_active{false};
};

/// Adapter state reported by BlueZ Adapter1.
struct BtAdapterInfo {
    std::string object_path;  ///< e.g. /org/bluez/hci0
    std::string name;         ///< e.g. hci0
    std::string address;
    std::string alias;
    bool powered{false};
    bool discoverable{false};
    bool pairable{false};
};

/// Validates and normalizes a Bluetooth MAC address to uppercase XX:XX:XX:XX:XX:XX.
bool normalize_mac_address(const std::string& input, std::string& out);

/// Converts XX:XX:XX:XX:XX:XX to XX_XX_XX_XX_XX_XX for BlueZ object paths and PulseAudio names.
std::string mac_to_underscore(const std::string& mac);

/// Extracts normalized MAC from a PulseAudio bluez source name (e.g. bluez_source.AA_BB_...a2dp_source).
std::optional<std::string> mac_from_pulse_bt_source(const std::string& source_name);

/// True if `source_name` is a Bluetooth A2DP source matching `target_mac` (or any BT source if empty).
bool is_matching_pulse_bt_source(const std::string& source_name, const std::string& target_mac);

/// Exponential backoff pacer for Bluetooth adapter bring-up and auto-reconnection.
class BtReconnectPacer {
public:
    /// Updates connection tracking; returns true on transition from connected to disconnected.
    bool note_connected(bool connected, int64_t now_ms);

    /// True when the backoff timer has expired and an attempt should run now.
    bool should_attempt(int64_t now_ms) const;

    /// Records that an attempt started at `now_ms`, advancing the backoff for any subsequent failure.
    void note_attempt(int64_t now_ms);

    /// Resets backoff to immediate (e.g. after a user `connect` request or fresh startup).
    void reset();

    int64_t current_delay_ms() const {
        return this->delay_ms_;
    }

private:
    bool was_connected_{false};
    bool has_attempted_{false};
    int64_t delay_ms_{BT_RECONNECT_INITIAL_MS};
    int64_t next_attempt_ms_{0};
};

/// Picks which known remote device should be auto-reconnected when none is connected.
const BtDeviceInfo* select_reconnect_target(const std::vector<BtDeviceInfo>& devices,
                                            const std::string& configured_mac,
                                            const std::string& remembered_mac,
                                            bool auto_connect);

}  // namespace bt_speaker_cli
