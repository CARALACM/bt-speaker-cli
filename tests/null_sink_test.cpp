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

#include "null_sink.h"

#include <gtest/gtest.h>

#include <array>

namespace bt_speaker_cli {
namespace {

TEST(NullSinkTest, CountsConsumedBytesAndReportsFrames) {
    NullAudioSink sink(NullSinkOutput::Discard);
    uint32_t reported_frames = 0;
    sink.on_frames_played = [&reported_frames](uint32_t frames, int64_t /*ts*/) {
        reported_frames += frames;
    };

    ASSERT_TRUE(sink.configure(48000, 2, 16));
    std::array<uint8_t, 400> pcm{};
    EXPECT_EQ(sink.write(pcm.data(), pcm.size(), 10), 400U);
    EXPECT_EQ(sink.total_bytes(), 400U);
    EXPECT_EQ(reported_frames, 100U);
    sink.stop();
}

}  // namespace
}  // namespace bt_speaker_cli
