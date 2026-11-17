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

#include "state_store.h"

#include "scoped_env.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace bt_speaker_cli {
namespace {

TEST(StateStoreTest, PersistsLastDeviceVolumeAndDelay) {
    const std::string dir = "state-test-" + std::to_string(getpid());
    ASSERT_EQ(::mkdir(dir.c_str(), 0700), 0);

    {
        StateStore store(state_store_path(dir));
        size_t malformed = 0;
        EXPECT_EQ(store.load(malformed), StateLoadResult::Absent);
        EXPECT_TRUE(store.set_last_device("AA:BB:CC:DD:EE:FF"));
        EXPECT_TRUE(store.set_volume_and_muted(65, true));
        EXPECT_TRUE(store.set_output_delay_ms(42));
    }

    {
        StateStore reloaded(state_store_path(dir));
        size_t malformed = 0;
        EXPECT_EQ(reloaded.load(malformed), StateLoadResult::Loaded);
        EXPECT_EQ(reloaded.last_device(), "AA:BB:CC:DD:EE:FF");
        EXPECT_EQ(reloaded.volume(), 65);
        EXPECT_EQ(reloaded.muted(), true);
        EXPECT_EQ(reloaded.output_delay_ms(), 42);
    }

    std::remove((dir + "/state").c_str());
    ::rmdir(dir.c_str());
}

TEST(StateStoreTest, DefaultPathUsesOwnSubdir) {
    const ScopedEnv xdg("XDG_STATE_HOME", "/tmp/xdg-state");
    EXPECT_EQ(state_store_path(""), "/tmp/xdg-state/bt-speaker-cli/state");
}

}  // namespace
}  // namespace bt_speaker_cli
