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

/// The socket calls the control channel makes, over POSIX AF_UNIX sockets.

#pragma once

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>

namespace bt_speaker_cli {

using socket_t = int;
inline constexpr socket_t INVALID_SOCKET_HANDLE = -1;

inline bool socket_startup() {
    return true;
}

inline void close_socket(socket_t socket) {
    ::close(socket);
}

inline bool set_nonblocking(socket_t socket) {
    const int flags = ::fcntl(socket, F_GETFL, 0);
    return flags >= 0 && ::fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

inline long receive_bytes(socket_t socket, char* buffer, size_t length) {
    return static_cast<long>(::read(socket, buffer, length));
}

inline long send_bytes(socket_t socket, const char* data, size_t length) {
    return static_cast<long>(::write(socket, data, length));
}

inline int socket_error() {
    return errno;
}

inline bool socket_interrupted(int error) {
    return error == EINTR;
}

inline bool socket_would_block(int error) {
    return error == EAGAIN || error == EWOULDBLOCK;
}

inline std::string socket_error_text(int error) {
    return std::strerror(error);
}

enum class ConnectFailure {
    Missing,  ///< no socket file at the path
    Refused,  ///< a file, but nothing accepting
    Denied,   ///< not this user's to connect to
    Other,
};

inline ConnectFailure classify_connect_error(int error) {
    switch (error) {
        case ENOENT:
            return ConnectFailure::Missing;
        case ECONNREFUSED:
            return ConnectFailure::Refused;
        case EACCES:
        case EPERM:
            return ConnectFailure::Denied;
        default:
            return ConnectFailure::Other;
    }
}

}  // namespace bt_speaker_cli
