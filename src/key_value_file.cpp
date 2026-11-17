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

#include "key_value_file.h"

#include <cstdio>
#include <string>
#include <vector>

namespace bt_speaker_cli {

namespace {

constexpr const char* BLANKS = " \t";

std::string trimmed(const std::string& text) {
    const size_t first = text.find_first_not_of(BLANKS);
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(BLANKS) - first + 1);
}

}  // namespace

KeyValueStatus read_key_value_file(const std::string& path, std::vector<KeyValueEntry>& entries,
                                   size_t& malformed_line) {
    entries.clear();
    if (path.empty()) {
        return KeyValueStatus::Unreadable;
    }

    std::FILE* file = std::fopen(path.c_str(), "r");
    if (file == nullptr) {
        return KeyValueStatus::Unreadable;
    }

    char buffer[MAX_KEY_VALUE_LINE_BYTES + 2] = {};
    size_t number = 0;
    KeyValueStatus status = KeyValueStatus::Ok;

    while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), file) != nullptr) {
        ++number;
        std::string line(buffer);

        const bool complete = !line.empty() && line.back() == '\n';
        if (!complete && line.size() > MAX_KEY_VALUE_LINE_BYTES) {
            malformed_line = number;
            status = KeyValueStatus::Malformed;
            break;
        }
        if (complete) {
            line.pop_back();
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string content = trimmed(line);
        if (content.empty() || content.front() == '#') {
            continue;
        }

        const size_t equals = content.find('=');
        if (equals == std::string::npos) {
            malformed_line = number;
            status = KeyValueStatus::Malformed;
            break;
        }
        KeyValueEntry entry;
        entry.key = trimmed(content.substr(0, equals));
        entry.value = trimmed(content.substr(equals + 1));
        entry.line = number;
        if (entry.key.empty()) {
            malformed_line = number;
            status = KeyValueStatus::Malformed;
            break;
        }
        entries.push_back(std::move(entry));
    }

    std::fclose(file);
    if (status != KeyValueStatus::Ok) {
        entries.clear();
    }
    return status;
}

}  // namespace bt_speaker_cli
