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

/// Detaching from the terminal (-z), and the exclusive file lock -P and the control socket use.

#pragma once

#include <string>

namespace bt_speaker_cli {

enum class PidFileStatus {
    Ok,
    AlreadyRunning,
    Failed,
};

PidFileStatus lock_file(const std::string& path, const char* what, unsigned mode, int& fd,
                        std::string& error);
void release_lock(int fd);

class PidFile {
public:
    PidFile() = default;
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    PidFileStatus acquire(const std::string& path, std::string& error);

private:
    std::string path_;
    int fd_{-1};
};

PidFileStatus probe_pidfile(const std::string& path, std::string& error);
bool daemonize(bool discard_stderr, std::string& error);

}  // namespace bt_speaker_cli
