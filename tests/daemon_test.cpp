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

#include "daemon.h"

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>

namespace bt_speaker_cli {
namespace {

TEST(PidFileTest, AcquireLocksAndUnlinksOnDestroy) {
    const std::string dir = "daemon-test-" + std::to_string(getpid());
    ASSERT_EQ(::mkdir(dir.c_str(), 0700), 0);
    const std::string path = dir + "/pid";

    {
        PidFile pid;
        std::string error;
        EXPECT_EQ(pid.acquire(path, error), PidFileStatus::Ok) << error;

        std::string conflict_err;
        EXPECT_EQ(probe_pidfile(path, conflict_err), PidFileStatus::AlreadyRunning);
    }

    std::string after_err;
    EXPECT_EQ(probe_pidfile(path, after_err), PidFileStatus::Ok) << after_err;
    std::remove(path.c_str());
    ::rmdir(dir.c_str());
}

}  // namespace
}  // namespace bt_speaker_cli
