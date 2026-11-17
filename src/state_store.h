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

/// What the daemon remembers about itself across restarts, in a file only it writes.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bt_speaker_cli {

enum class StateLoadResult {
    Loaded,
    Absent,
    Corrupt,
};

/// `<state_dir>/state`, else under `$XDG_STATE_HOME` or `~/.local/state`; empty when none apply.
std::string state_store_path(const std::string& state_dir);

/// The daemon's memory, rewritten atomically on every change; an unwritable file is not fatal.
/// Not thread-safe: main loop only.
class StateStore {
public:
    explicit StateStore(std::string path, bool create_parents = false);

    StateLoadResult load(size_t& malformed_line);

    const std::string& path() const {
        return this->path_;
    }

    /// MAC address of the last paired/connected remote Bluetooth device, empty if none.
    std::string last_device() const;
    bool set_last_device(const std::string& mac);

    /// Output delay in milliseconds, 0-5000.
    std::optional<uint16_t> output_delay_ms() const;
    bool set_output_delay_ms(uint16_t delay_ms);

    /// Last applied gain, 0-100; volume and mute can each be absent independently.
    std::optional<uint8_t> volume() const;
    std::optional<bool> muted() const;
    bool set_volume_and_muted(uint8_t volume, bool muted);

private:
    std::optional<std::string> get(const std::string& key) const;
    bool set_all(const std::vector<std::pair<std::string, std::string>>& pairs);
    bool write() const;
    std::optional<uint64_t> get_number(const std::string& key, uint64_t limit) const;

    std::string path_;
    bool create_parents_;
    std::map<std::string, std::string> values_;
};

}  // namespace bt_speaker_cli
