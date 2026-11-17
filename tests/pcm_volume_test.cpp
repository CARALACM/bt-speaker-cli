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

#include "pcm_volume.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace bt_speaker_cli {
namespace {

TEST(PcmVolumeTest, Scales16BitSamplesWithMuteAndUnity) {
    EXPECT_EQ(q32_gain_for(100, false), Q32_ONE);
    EXPECT_EQ(q32_gain_for(100, true), 0U);
    EXPECT_EQ(q32_gain_for(0, false), 0U);

    std::array<uint8_t, 4> pcm{0x00, 0x40, 0x00, 0xC0};  // +16384, -16384
    apply_volume(pcm.data(), pcm.size(), 2, q32_gain_for(50, false));
    const auto s0 = static_cast<int16_t>(pcm[0] | (pcm[1] << 8));
    const auto s1 = static_cast<int16_t>(pcm[2] | (pcm[3] << 8));
    EXPECT_NEAR(s0, 8192, 2);
    EXPECT_NEAR(s1, -8192, 2);
}

}  // namespace
}  // namespace bt_speaker_cli
