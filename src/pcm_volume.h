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

/// Software volume for interleaved signed little-endian PCM, in Q32 fixed point.

#pragma once

#include <cstddef>
#include <cstdint>

namespace bt_speaker_cli {

inline constexpr uint64_t Q32_ONE = UINT64_C(1) << 32;

/// Q32 gain for a 0-100 volume: linear (volume / 100), 0 when muted.
uint64_t q32_gain_for(uint8_t volume, bool muted);

/// Scales `len` bytes of signed little-endian PCM in place by a Q32 gain of at most Q32_ONE.
void apply_volume(uint8_t* data, size_t len, uint8_t bytes_per_sample, uint64_t scale);

inline constexpr uint32_t VOLUME_RAMP_MS = 20;

uint64_t volume_ramp_step(uint32_t sample_rate);
uint64_t ramped_gain(uint64_t current, uint64_t target, uint64_t step, size_t frames);
uint64_t apply_volume_ramp(uint8_t* data, size_t len, uint8_t bytes_per_sample, uint8_t channels,
                           uint64_t current, uint64_t target, uint64_t step);

}  // namespace bt_speaker_cli
