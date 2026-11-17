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

/// Shell commands run on stream start and stop (--hook-start / --hook-stop).

#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

namespace bt_speaker_cli {

/// Stream facts exported to a hook; empty fields are left out of the environment.
struct HookContext {
    std::string device_address;  ///< BT_SPEAKER_DEVICE_ADDRESS
    std::string device_name;     ///< BT_SPEAKER_DEVICE_NAME
    std::string adapter;         ///< BT_SPEAKER_ADAPTER
    std::string speaker_name;    ///< BT_SPEAKER_NAME
};

/// Runs hooks as `/bin/sh -c`, one at a time in event order, without blocking the caller.
class HookRunner {
public:
    void run(const std::string& command, const char* event, const HookContext& context);
    void poll();
    void flush();

    size_t running() const {
        return this->running_.size();
    }

private:
    struct RunningHook {
        pid_t pid;
        std::string event;
    };

    struct PendingHook {
        std::string command;
        std::string event;
        HookContext context;
    };

    void spawn(const std::string& command, const char* event, const HookContext& context);
    bool reap(RunningHook& hook);

    std::vector<RunningHook> running_;
    std::optional<PendingHook> pending_;
};

}  // namespace bt_speaker_cli
