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

/// When a sink whose device died mid-stream should try to get it back, and when to give up.

#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace bt_speaker_cli {

inline constexpr int64_t SINK_RESCAN_DELAY_MS = 2000;
inline constexpr int64_t SINK_RESCAN_MAX_DELAY_MS = 30000;
inline constexpr int SINK_RESCAN_ATTEMPTS = 5;

class SinkRecovery {
public:
    bool reopen_due();
    void reopen_done(bool recovered);
    bool rescan_due(int64_t now_ms);
    void rescan_done(bool recovered);
    void rescan_abandoned();
    void rescan_soon();
    bool pending() const;
    void discard_frames(uint32_t frames);
    uint32_t take_discarded_frames();
    void forget_discarded_frames();
    void cancel();
    void reset();

private:
    void escalate_();
    void refill_();

    static constexpr int64_t NOT_STAMPED = INT64_MIN;
    static int64_t delay_for_(int attempts_made);

    bool reopen_spent_{false};
    bool rescan_spent_{false};
    bool rescan_in_flight_{false};
    bool rescan_immediate_{false};
    int rescan_attempts_{0};
    uint32_t discarded_frames_{0};
    std::atomic<bool> rescan_owed_{false};
    int64_t rescan_at_ms_{NOT_STAMPED};
};

class OutageGapHandoff {
public:
    void add(uint32_t frames);
    uint32_t take();
    void forget();

private:
    std::atomic<uint32_t> frames_{0};
};

constexpr uint32_t frames_with_gap(uint32_t gap_frames, uint64_t played_frames) {
    const uint64_t total = static_cast<uint64_t>(gap_frames) + played_frames;
    const uint64_t ceiling = std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>((total < ceiling) ? total : ceiling);
}

}  // namespace bt_speaker_cli
