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

/// Lock-free single-producer/single-consumer byte ring, for the pull-model backends.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace bt_speaker_cli {

/// Lock-free SPSC byte ring bridging write()'s push to a backend callback's pull.
class PcmRingBuffer {
public:
    size_t write(const uint8_t* data, size_t len);
    size_t read(uint8_t* dest, size_t len);
    size_t available() const;
    size_t free_space() const;
    void request_clear();
    void drop();
    void reset(size_t capacity);

private:
    std::vector<uint8_t> buffer_;
    size_t capacity_{0};
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
    std::atomic<bool> clear_requested_{false};
};

}  // namespace bt_speaker_cli
