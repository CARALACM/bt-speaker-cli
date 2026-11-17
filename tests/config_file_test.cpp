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

#include "config_file.h"

#include "cli.h"
#include "parse_harness.h"
#include "scoped_env.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace bt_speaker_cli {
namespace {

TEST(ConfigFileTest, LoadsAndAppliesUnderCliFlags) {
    const std::string dir = "cfg-test-" + std::to_string(getpid());
    ASSERT_EQ(::mkdir(dir.c_str(), 0700), 0);
    const std::string cfg_path = dir + "/config";

    std::FILE* f = std::fopen(cfg_path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fprintf(f, "# comment\nname = desk-bt\noutput = null\nbuffer-ms = 250\n");
    std::fclose(f);

    Parse p1({"--config", cfg_path});
    ASSERT_TRUE(p1.ok()) << p1.diagnostics();
    EXPECT_EQ(p1.options().name, "desk-bt");
    EXPECT_EQ(p1.options().device, "null");
    EXPECT_EQ(p1.options().buffer_ms, 250U);

    // CLI wins over config file.
    Parse p2({"--config", cfg_path, "-n", "override-bt"});
    ASSERT_TRUE(p2.ok()) << p2.diagnostics();
    EXPECT_EQ(p2.options().name, "override-bt");
    EXPECT_EQ(p2.options().buffer_ms, 250U);

    std::remove(cfg_path.c_str());
    ::rmdir(dir.c_str());
}

TEST(ConfigFileTest, SearchPathsAreIndependentFromOtherAudioDaemons) {
    const ScopedEnv xdg("XDG_CONFIG_HOME", "/tmp/xdg-cfg");
    const auto paths = config_search_paths();
    ASSERT_FALSE(paths.empty());
    EXPECT_EQ(paths.front(), "/tmp/xdg-cfg/bt-speaker-cli/config");
    EXPECT_EQ(paths.back(), "/etc/bt-speaker-cli.conf");
}

}  // namespace
}  // namespace bt_speaker_cli
