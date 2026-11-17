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

#include "cli.h"

#include "control.h"
#include "log.h"
#include "parse_harness.h"
#include "scoped_env.h"

#include <gtest/gtest.h>

#include <string>

namespace bt_speaker_cli {
namespace {

TEST(ParseOptions, DefaultsWithoutConfig) {
    const ScopedEnv xdg("XDG_CONFIG_HOME", "/nonexistent-bt-speaker-cli-cfg");
    const ScopedEnv home("HOME", "/nonexistent-bt-speaker-cli-home");

    Parse parse({});
    ASSERT_TRUE(parse.ok()) << parse.diagnostics();
    EXPECT_EQ(parse.options().device, DEFAULT_OUTPUT_DEVICE);
    EXPECT_EQ(parse.options().log_level, LogLevel::INFO);
    EXPECT_TRUE(parse.options().auto_connect);
    EXPECT_TRUE(parse.options().discoverable);
    EXPECT_TRUE(parse.options().allow_unpaired);
    EXPECT_FALSE(parse.options().no_dbus);
    EXPECT_FALSE(parse.options().name.empty());
}

TEST(ParseOptions, SetsBluetoothAndAudioFlags) {
    const ScopedEnv xdg("XDG_CONFIG_HOME", "/nonexistent-bt-speaker-cli-cfg");
    const ScopedEnv home("HOME", "/nonexistent-bt-speaker-cli-home");

    Parse parse({"-o", "null", "-n", "study-bt", "-a", "hci1", "-D", "aa:bb:cc:dd:ee:ff",
                 "--auto-connect", "false", "--discoverable", "false", "--require-confirm",
                 "--no-dbus", "--buffer-ms", "200", "--output-delay", "35", "--fixed-volume",
                 "--release-device", "-d", "debug"});

    ASSERT_TRUE(parse.ok()) << parse.diagnostics();
    EXPECT_EQ(parse.options().device, "null");
    EXPECT_EQ(parse.options().name, "study-bt");
    EXPECT_EQ(parse.options().adapter, "hci1");
    EXPECT_EQ(parse.options().target_device, "AA:BB:CC:DD:EE:FF");
    EXPECT_FALSE(parse.options().auto_connect);
    EXPECT_FALSE(parse.options().discoverable);
    EXPECT_FALSE(parse.options().allow_unpaired);
    EXPECT_TRUE(parse.options().no_dbus);
    EXPECT_EQ(parse.options().buffer_ms, 200U);
    EXPECT_EQ(parse.options().output_delay_ms, 35U);
    EXPECT_TRUE(parse.options().fixed_volume);
    EXPECT_TRUE(parse.options().release_device);
    EXPECT_EQ(parse.options().log_level, LogLevel::DEBUG);
}

TEST(ParseOptions, RejectsInvalidMacAndContradictions) {
    const ScopedEnv xdg("XDG_CONFIG_HOME", "/nonexistent-bt-speaker-cli-cfg");
    const ScopedEnv home("HOME", "/nonexistent-bt-speaker-cli-home");

    EXPECT_FALSE(Parse({"-D", "not-a-mac"}).ok());
    EXPECT_FALSE(Parse({"--allow-unpaired", "--require-confirm"}).ok());
    EXPECT_FALSE(Parse({"--no-control", "--control-socket", "/tmp/a.sock"}).ok());
    EXPECT_FALSE(Parse({"--buffer-ms", "5"}).ok());
    EXPECT_FALSE(Parse({"--output-delay", "9999"}).ok());
    EXPECT_FALSE(Parse({"-z", "-o", "stdout"}).ok());
}

TEST(ParseOptions, ParsesSubcommandsFirst) {
    const ScopedEnv xdg("XDG_CONFIG_HOME", "/nonexistent-bt-speaker-cli-cfg");
    const ScopedEnv home("HOME", "/nonexistent-bt-speaker-cli-home");

    Parse p1({"status", "--control-socket", "/tmp/bt.sock"});
    ASSERT_TRUE(p1.ok()) << p1.diagnostics();
    EXPECT_EQ(p1.options().subcommand, "status");
    EXPECT_EQ(p1.options().control_socket, "/tmp/bt.sock");

    Parse p2({"connect", "aa:bb:cc:11:22:33"});
    ASSERT_TRUE(p2.ok()) << p2.diagnostics();
    EXPECT_EQ(p2.options().subcommand, "connect");
    ASSERT_EQ(p2.options().subcommand_args.size(), 1U);

    Parse p3({"connect"});
    ASSERT_TRUE(p3.ok()) << p3.diagnostics();
    EXPECT_EQ(p3.options().subcommand, "connect");
    EXPECT_TRUE(p3.options().subcommand_args.empty());
}

}  // namespace
}  // namespace bt_speaker_cli
