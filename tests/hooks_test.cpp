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

#include "hooks.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace bt_speaker_cli {
namespace {

TEST(HooksTest, ExportsEnvironmentVariablesWithoutBlocking) {
    const std::string dir = "hooks-test-" + std::to_string(getpid());
    ASSERT_EQ(::mkdir(dir.c_str(), 0700), 0);
    const std::string marker = dir + "/out.txt";

    HookRunner runner;
    HookContext ctx;
    ctx.device_address = "AA:BB:CC:DD:EE:FF";
    ctx.device_name = "MyPC";
    ctx.adapter = "hci0";
    ctx.speaker_name = "room-bt";

    const std::string cmd =
        "printf '%s|%s|%s|%s|%s' \"$BT_SPEAKER_EVENT\" \"$BT_SPEAKER_DEVICE_ADDRESS\" "
        "\"$BT_SPEAKER_DEVICE_NAME\" \"$BT_SPEAKER_ADAPTER\" \"$BT_SPEAKER_NAME\" > " +
        marker;

    runner.run(cmd, "start", ctx);
    for (int i = 0; i < 100 && runner.running() > 0; ++i) {
        runner.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(runner.running(), 0U);

    std::FILE* f = std::fopen(marker.c_str(), "r");
    ASSERT_NE(f, nullptr);
    char buf[256] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    EXPECT_EQ(std::string(buf, n), "start|AA:BB:CC:DD:EE:FF|MyPC|hci0|room-bt");

    std::remove(marker.c_str());
    ::rmdir(dir.c_str());
}

}  // namespace
}  // namespace bt_speaker_cli
