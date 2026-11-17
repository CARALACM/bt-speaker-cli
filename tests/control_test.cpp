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

#include "control.h"

#include "scoped_env.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace bt_speaker_cli {
namespace {

TEST(ControlTest, ParsesAndEncodesRequests) {
    ControlRequest req;
    std::string err;

    ASSERT_TRUE(parse_control_request("vol", {"75"}, req, err)) << err;
    EXPECT_EQ(req.command, ControlCommand::Volume);
    EXPECT_EQ(req.volume, 75);
    EXPECT_EQ(encode_control_request(req), "vol 75");

    ASSERT_TRUE(parse_control_request("connect", {"aa-bb-cc-dd-ee-ff"}, req, err)) << err;
    EXPECT_EQ(req.command, ControlCommand::Connect);
    EXPECT_EQ(req.mac, "AA:BB:CC:DD:EE:FF");
    EXPECT_EQ(encode_control_request(req), "connect AA:BB:CC:DD:EE:FF");

    ASSERT_TRUE(parse_control_request("connect", {}, req, err)) << err;
    EXPECT_EQ(req.command, ControlCommand::Connect);
    EXPECT_TRUE(req.mac.empty());

    ASSERT_TRUE(parse_control_request("discoverable", {"off"}, req, err)) << err;
    EXPECT_EQ(req.command, ControlCommand::Discoverable);
    EXPECT_EQ(req.flag, false);

    ASSERT_TRUE(parse_control_request("pair", {"confirm"}, req, err)) << err;
    EXPECT_EQ(req.command, ControlCommand::Pair);
    EXPECT_EQ(req.confirm, true);

    EXPECT_FALSE(parse_control_request("vol", {"101"}, req, err));
    EXPECT_FALSE(parse_control_request("trust", {"invalid"}, req, err));
}

TEST(ControlTest, FormatsStatusAndDevices) {
    StatusSnapshot s;
    s.name = "room-bt";
    s.dbus_available = true;
    s.adapter = BtAdapterInfo{"/org/bluez/hci0", "hci0", "11:22:33:44:55:66", "room-bt", true,
                              true, true};
    s.discoverable = true;
    s.connected_device =
        BtDeviceInfo{"/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF", "AA:BB:CC:DD:EE:FF", "Laptop",
                     true, true, true, 0, true};
    s.streaming = true;
    s.bt_source = "bluez_source.AA_BB_CC_DD_EE_FF.a2dp_source";
    s.format = StreamFormat{48000, 2, 16};
    s.media.status = "playing";
    s.media.artist = "Artist";
    s.media.title = "Song";
    s.volume = 80;
    s.output = "pulse";

    const std::string out = format_status(s);
    EXPECT_NE(out.find("name: room-bt\n"), std::string::npos);
    EXPECT_NE(out.find("peer: Laptop (AA:BB:CC:DD:EE:FF)\n"), std::string::npos);
    EXPECT_NE(out.find("stream: receiving\n"), std::string::npos);
    EXPECT_NE(out.find("track: Artist - Song\n"), std::string::npos);
    EXPECT_NE(out.find("output: pulse (48000 Hz / 2 ch / 16-bit)\n"), std::string::npos);
}

TEST(ControlTest, DefaultSocketNameUsesDedicatedPath) {
    const ScopedEnv runtime("XDG_RUNTIME_DIR", "/run/user/1000");
    EXPECT_EQ(control_socket_path(control_runtime_dir().path),
              "/run/user/1000/bt-speaker-cli.sock");
}

}  // namespace
}  // namespace bt_speaker_cli
