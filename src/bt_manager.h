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

/// Non-blocking BlueZ D-Bus client: adapter setup, pairing agent, AVRCP, and auto-reconnect.

#pragma once

#include "bt_reconnect.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bt_speaker_cli {

enum class AvrcpCommand {
    Play,
    Pause,
    Stop,
    Next,
    Previous,
};

struct BtManagerConfig {
    std::string adapter;        ///< empty = first available (e.g. hci0)
    std::string speaker_name;   ///< BlueZ Adapter1.Alias
    std::string target_device;  ///< normalized MAC filter, or empty for any
    bool auto_connect{true};
    bool discoverable{true};
    bool allow_unpaired{true};  ///< true = Just Works auto-accept; false = wait for pair confirm
    bool no_dbus{false};        ///< disable D-Bus (for testing)
};

struct BtPendingPairing {
    std::string address;
    std::string name;
    std::string passkey;
    bool active{false};
};

class BtManager {
public:
    explicit BtManager(BtManagerConfig config);
    ~BtManager();

    BtManager(const BtManager&) = delete;
    BtManager& operator=(const BtManager&) = delete;

    /// Initializes D-Bus connection if enabled; never fatal if bluetoothd is not up yet.
    void start(const std::string& remembered_device);

    /// Pumps D-Bus messages, retries adapter setup, and drives auto-reconnection. Main loop only.
    void poll(int64_t now_ms);

    /// Unregisters the pairing agent and closes D-Bus.
    void stop();

    bool available() const;
    bool adapter_ready() const;
    std::optional<BtAdapterInfo> adapter() const;
    std::vector<BtDeviceInfo> devices() const;
    std::optional<BtDeviceInfo> connected_device() const;
    BtMediaInfo media() const;
    BtPendingPairing pending_pairing() const;
    bool discoverable() const;

    /// Initiates an asynchronous connection to `mac` (or best candidate if `mac` is empty).
    bool connect_device(const std::string& mac, std::string& error);

    /// Disconnects the currently connected device (or `mac` if given).
    bool disconnect_device(const std::string& mac, std::string& error);

    /// Marks `mac` as Trusted (or untrusted).
    bool set_trusted(const std::string& mac, bool trusted, std::string& error);

    /// Removes pairing for `mac` from BlueZ.
    bool remove_device(const std::string& mac, std::string& error);

    /// Enables or disables Bluetooth discoverability on the active adapter.
    bool set_discoverable(bool enabled, std::string& error);

    /// Confirms or rejects a pending pairing request.
    bool answer_pairing(bool confirm, std::string& error);

    /// Sends an AVRCP playback verb to the active MediaPlayer1.
    bool send_media_command(AvrcpCommand command, std::string& error);

    /// Pushes A2DP MediaTransport1.Volume (0-100 mapped to 0-127) if supported.
    bool set_transport_volume(uint8_t volume);

    /// Fired when a device connects (with its MAC) so StateStore can remember it.
    std::function<void(const std::string& mac)> on_device_connected;

    /// Fired when the remote A2DP peer updates MediaTransport1.Volume (0-100).
    std::function<void(uint8_t volume)> on_transport_volume_changed;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace bt_speaker_cli
