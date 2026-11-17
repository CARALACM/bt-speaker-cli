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

#include "bt_reconnect.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace bt_speaker_cli {

namespace {

bool is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char to_upper_ascii(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

}  // namespace

bool normalize_mac_address(const std::string& input, std::string& out) {
    if (input.size() != 17) {
        return false;
    }
    std::string normalized;
    normalized.reserve(17);
    for (size_t i = 0; i < 17; ++i) {
        const char c = input[i];
        if ((i % 3) == 2) {
            if (c != ':' && c != '-' && c != '_') {
                return false;
            }
            normalized.push_back(':');
        } else {
            if (!is_hex_digit(c)) {
                return false;
            }
            normalized.push_back(to_upper_ascii(c));
        }
    }
    out = std::move(normalized);
    return true;
}

std::string mac_to_underscore(const std::string& mac) {
    std::string out = mac;
    std::replace(out.begin(), out.end(), ':', '_');
    return out;
}

std::optional<std::string> mac_from_pulse_bt_source(const std::string& source_name) {
    static constexpr const char* PREFIX_SOURCE = "bluez_source.";
    static constexpr const char* PREFIX_INPUT = "bluez_input.";

    size_t offset = 0;
    if (source_name.rfind(PREFIX_SOURCE, 0) == 0) {
        offset = std::char_traits<char>::length(PREFIX_SOURCE);
    } else if (source_name.rfind(PREFIX_INPUT, 0) == 0) {
        offset = std::char_traits<char>::length(PREFIX_INPUT);
    } else {
        return std::nullopt;
    }

    if (source_name.size() < offset + 17) {
        return std::nullopt;
    }
    const std::string candidate = source_name.substr(offset, 17);
    // Reject underscore-separated mismatch where separator isn't '_' or ':'.
    for (size_t i = 2; i < 17; i += 3) {
        if (candidate[i] != '_' && candidate[i] != ':') {
            return std::nullopt;
        }
    }
    std::string mac;
    if (!normalize_mac_address(candidate, mac)) {
        return std::nullopt;
    }
    // Must end at string end or a dot (e.g. ".a2dp_source").
    if (source_name.size() > offset + 17 && source_name[offset + 17] != '.') {
        return std::nullopt;
    }
    return mac;
}

bool is_matching_pulse_bt_source(const std::string& source_name, const std::string& target_mac) {
    // Ignore handsfree/headset SCO sources when an A2DP source is expected; allow bluez_source/input
    // unless explicitly ".headset_head_unit" / ".handsfree_head_unit".
    if (source_name.find(".monitor") != std::string::npos) {
        return false;
    }
    if (source_name.find("head_unit") != std::string::npos ||
        source_name.find("headset") != std::string::npos) {
        return false;
    }
    const std::optional<std::string> source_mac = mac_from_pulse_bt_source(source_name);
    if (!source_mac.has_value()) {
        return false;
    }
    if (target_mac.empty()) {
        return true;
    }
    std::string normalized_target;
    if (!normalize_mac_address(target_mac, normalized_target)) {
        return false;
    }
    return *source_mac == normalized_target;
}

bool BtReconnectPacer::note_connected(bool connected, int64_t now_ms) {
    if (connected) {
        this->was_connected_ = true;
        this->has_attempted_ = false;
        this->delay_ms_ = BT_RECONNECT_INITIAL_MS;
        this->next_attempt_ms_ = 0;
        return false;
    }
    if (this->was_connected_) {
        this->was_connected_ = false;
        this->has_attempted_ = true;
        this->delay_ms_ = BT_RECONNECT_INITIAL_MS;
        this->next_attempt_ms_ = now_ms + this->delay_ms_;
        return true;
    }
    return false;
}

bool BtReconnectPacer::should_attempt(int64_t now_ms) const {
    if (!this->has_attempted_) {
        return true;
    }
    return now_ms >= this->next_attempt_ms_;
}

void BtReconnectPacer::note_attempt(int64_t now_ms) {
    if (!this->has_attempted_) {
        this->has_attempted_ = true;
        this->delay_ms_ = BT_RECONNECT_INITIAL_MS;
    } else {
        this->delay_ms_ = std::min(this->delay_ms_ * 2, BT_RECONNECT_MAX_MS);
    }
    this->next_attempt_ms_ = now_ms + this->delay_ms_;
}

void BtReconnectPacer::reset() {
    this->was_connected_ = false;
    this->has_attempted_ = false;
    this->delay_ms_ = BT_RECONNECT_INITIAL_MS;
    this->next_attempt_ms_ = 0;
}

const BtDeviceInfo* select_reconnect_target(const std::vector<BtDeviceInfo>& devices,
                                            const std::string& configured_mac,
                                            const std::string& remembered_mac,
                                            bool auto_connect) {
    if (!configured_mac.empty()) {
        for (const BtDeviceInfo& dev : devices) {
            if (dev.address == configured_mac) {
                return dev.connected ? nullptr : &dev;
            }
        }
        return nullptr;
    }

    if (!auto_connect) {
        return nullptr;
    }

    // Prefer the last connected device if it is paired and trusted.
    if (!remembered_mac.empty()) {
        for (const BtDeviceInfo& dev : devices) {
            if (dev.address == remembered_mac && dev.paired && dev.trusted) {
                return dev.connected ? nullptr : &dev;
            }
        }
    }

    // Otherwise fall back to the first paired and trusted device.
    for (const BtDeviceInfo& dev : devices) {
        if (dev.paired && dev.trusted && !dev.connected) {
            return &dev;
        }
    }
    return nullptr;
}

}  // namespace bt_speaker_cli
