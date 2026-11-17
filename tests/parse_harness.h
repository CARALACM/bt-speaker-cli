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

#pragma once

#include "cli.h"

#include <cstdio>
#include <string>
#include <vector>

namespace bt_speaker_cli {

class Parse {
public:
    explicit Parse(const std::vector<std::string>& args) {
        std::vector<std::string> storage;
        storage.reserve(args.size() + 1);
        storage.emplace_back("bt-speaker-cli");
        storage.insert(storage.end(), args.begin(), args.end());

        std::vector<char*> argv;
        argv.reserve(storage.size());
        for (std::string& word : storage) {
            argv.push_back(word.data());
        }

        std::FILE* err = std::tmpfile();
        this->ok_ =
            parse_options(static_cast<int>(argv.size()), argv.data(), this->options_, err);
        if (err != nullptr) {
            std::rewind(err);
            char buf[256];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof(buf), err)) > 0) {
                this->diagnostics_.append(buf, n);
            }
            std::fclose(err);
        }
    }

    bool ok() const {
        return this->ok_;
    }

    const Options& options() const {
        return this->options_;
    }

    const std::string& diagnostics() const {
        return this->diagnostics_;
    }

private:
    bool ok_{false};
    Options options_;
    std::string diagnostics_;
};

}  // namespace bt_speaker_cli
