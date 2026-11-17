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

#include "sink_recovery.h"

#include <gtest/gtest.h>

namespace bt_speaker_cli {
namespace {

TEST(SinkRecoveryTest, EscalatesAfterFailedReopenAndRetriesWithBackoff) {
    SinkRecovery r;
    EXPECT_TRUE(r.reopen_due());
    r.reopen_done(false);
    EXPECT_TRUE(r.pending());

    EXPECT_FALSE(r.rescan_due(0));
    EXPECT_FALSE(r.rescan_due(SINK_RESCAN_DELAY_MS - 1));
    EXPECT_TRUE(r.rescan_due(SINK_RESCAN_DELAY_MS));
    r.rescan_done(true);
    EXPECT_FALSE(r.pending());
    EXPECT_TRUE(r.reopen_due());
}

}  // namespace
}  // namespace bt_speaker_cli
