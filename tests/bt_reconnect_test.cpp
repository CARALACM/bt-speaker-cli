// Copyright 2026 CARALACM
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

#include "bt_reconnect.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace bt_speaker_cli {
namespace {

TEST(MacAddressTest, NormalizesColonsDashesAndUnderscores) {
    std::string norm;
    ASSERT_TRUE(normalize_mac_address("aa:bb:cc:dd:ee:ff", norm));
    EXPECT_EQ(norm, "AA:BB:CC:DD:EE:FF");

    ASSERT_TRUE(normalize_mac_address("11-22-33-aa-bb-cc", norm));
    EXPECT_EQ(norm, "11:22:33:AA:BB:CC");

    ASSERT_TRUE(normalize_mac_address("aa_bb_cc_00_11_22", norm));
    EXPECT_EQ(norm, "AA:BB:CC:00:11:22");
    EXPECT_EQ(mac_to_underscore(norm), "AA_BB_CC_00_11_22");
}

TEST(MacAddressTest, RejectsInvalidMacs) {
    std::string norm;
    EXPECT_FALSE(normalize_mac_address("", norm));
    EXPECT_FALSE(normalize_mac_address("AA:BB:CC:DD:EE", norm));
    EXPECT_FALSE(normalize_mac_address("GG:BB:CC:DD:EE:FF", norm));
    EXPECT_FALSE(normalize_mac_address("AABBCCDDEEFF", norm));
}

TEST(PulseSourceMatchTest, ExtractsMacAndFiltersHeadsetSco) {
    const auto mac1 = mac_from_pulse_bt_source("bluez_source.AA_BB_CC_11_22_33.a2dp_source");
    ASSERT_TRUE(mac1.has_value());
    EXPECT_EQ(*mac1, "AA:BB:CC:11:22:33");

    const auto mac2 = mac_from_pulse_bt_source("bluez_input.aa_bb_cc_11_22_33.0");
    ASSERT_TRUE(mac2.has_value());
    EXPECT_EQ(*mac2, "AA:BB:CC:11:22:33");

    EXPECT_FALSE(mac_from_pulse_bt_source("alsa_input.pci-0000.analog-stereo").has_value());

    EXPECT_TRUE(is_matching_pulse_bt_source("bluez_source.AA_BB_CC_11_22_33.a2dp_source", ""));
    EXPECT_TRUE(is_matching_pulse_bt_source("bluez_source.AA_BB_CC_11_22_33.a2dp_source",
                                            "aa:bb:cc:11:22:33"));
    EXPECT_FALSE(is_matching_pulse_bt_source("bluez_source.AA_BB_CC_11_22_33.a2dp_source",
                                             "99:88:77:66:55:44"));
    EXPECT_FALSE(is_matching_pulse_bt_source("bluez_source.AA_BB_CC_11_22_33.headset_head_unit",
                                             ""));
    EXPECT_FALSE(is_matching_pulse_bt_source("bluez_source.AA_BB_CC_11_22_33.a2dp_source.monitor",
                                             ""));
}

TEST(BtReconnectPacerTest, ExponentialBackoffAndResetOnConnect) {
    BtReconnectPacer pacer;
    EXPECT_TRUE(pacer.should_attempt(0));

    pacer.note_attempt(1000);
    EXPECT_EQ(pacer.current_delay_ms(), BT_RECONNECT_INITIAL_MS);
    EXPECT_FALSE(pacer.should_attempt(1000 + BT_RECONNECT_INITIAL_MS - 1));
    EXPECT_TRUE(pacer.should_attempt(1000 + BT_RECONNECT_INITIAL_MS));

    pacer.note_attempt(5000);
    EXPECT_EQ(pacer.current_delay_ms(), BT_RECONNECT_INITIAL_MS * 2);

    // Connecting clears backoff; disconnecting arms initial delay.
    EXPECT_FALSE(pacer.note_connected(true, 10000));
    EXPECT_TRUE(pacer.note_connected(false, 20000));
    EXPECT_EQ(pacer.current_delay_ms(), BT_RECONNECT_INITIAL_MS);
    EXPECT_FALSE(pacer.should_attempt(21000));
    EXPECT_TRUE(pacer.should_attempt(20000 + BT_RECONNECT_INITIAL_MS));
}

TEST(SelectReconnectTargetTest, HonorsConfiguredAndRememberedPreferences) {
    const std::vector<BtDeviceInfo> devices = {
        {"/org/bluez/hci0/dev_11_11_11_11_11_11", "11:11:11:11:11:11", "Old", true, true, false,
         0, true},
        {"/org/bluez/hci0/dev_22_22_22_22_22_22", "22:22:22:22:22:22", "Last", true, true, false,
         0, true},
        {"/org/bluez/hci0/dev_33_33_33_33_33_33", "33:33:33:33:33:33", "Untrusted", true, false,
         false, 0, true},
    };

    const BtDeviceInfo* last = select_reconnect_target(devices, "", "22:22:22:22:22:22", true);
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(last->address, "22:22:22:22:22:22");

    const BtDeviceInfo* pinned =
        select_reconnect_target(devices, "11:11:11:11:11:11", "22:22:22:22:22:22", true);
    ASSERT_NE(pinned, nullptr);
    EXPECT_EQ(pinned->address, "11:11:11:11:11:11");

    EXPECT_EQ(select_reconnect_target(devices, "", "22:22:22:22:22:22", false), nullptr);
    EXPECT_NE(select_reconnect_target(devices, "11:11:11:11:11:11", "", false), nullptr);
}

}  // namespace
}  // namespace bt_speaker_cli
