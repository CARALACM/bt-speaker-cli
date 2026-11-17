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

/// Read-only operator config: long flag names as keys, values exactly as getopt would see them.

#pragma once

#include "key_value_file.h"

#include <string>
#include <vector>

namespace bt_speaker_cli {

inline constexpr const char* SYSTEM_CONFIG_PATH = "/etc/bt-speaker-cli.conf";

struct ConfigFile {
    std::string path;
    std::vector<KeyValueEntry> entries;
};

/// Where a config file is looked for, in order; empty variables contribute nothing.
std::vector<std::string> config_search_paths();

/// Finds and reads the first config file, used whole; finding none is not an error.
bool load_config_file(const std::string& explicit_path,
                      const std::vector<std::string>& search_paths, ConfigFile& out,
                      std::string& error);

}  // namespace bt_speaker_cli
