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

#include "pcm_ring.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

namespace bt_speaker_cli {
namespace {

TEST(PcmRingTest, WritesReadsAndWrapsCleanly) {
    PcmRingBuffer ring;
    ring.reset(9);

    const std::array<uint8_t, 5> in{1, 2, 3, 4, 5};
    EXPECT_EQ(ring.write(in.data(), in.size()), 5U);
    EXPECT_EQ(ring.available(), 5U);
    EXPECT_EQ(ring.free_space(), 3U);

    std::array<uint8_t, 4> out{};
    EXPECT_EQ(ring.read(out.data(), out.size()), 4U);
    EXPECT_EQ(out, (std::array<uint8_t, 4>{1, 2, 3, 4}));

    EXPECT_EQ(ring.write(in.data(), in.size()), 5U);
    std::array<uint8_t, 6> tail{};
    EXPECT_EQ(ring.read(tail.data(), tail.size()), 6U);
    EXPECT_EQ(tail, (std::array<uint8_t, 6>{5, 1, 2, 3, 4, 5}));
}

}  // namespace
}  // namespace bt_speaker_cli
