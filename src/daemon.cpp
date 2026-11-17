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

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace bt_speaker_cli {

namespace {

constexpr mode_t PIDFILE_MODE = 0644;

}  // namespace

PidFileStatus lock_file(const std::string& path, const char* what, unsigned mode, int& fd,
                        std::string& error) {
    const int opened =
        ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, static_cast<mode_t>(mode));
    if (opened < 0) {
        error = std::string("cannot open ") + what + " " + path + ": " + std::strerror(errno);
        return PidFileStatus::Failed;
    }

    if (::flock(opened, LOCK_EX | LOCK_NB) == 0) {
        fd = opened;
        return PidFileStatus::Ok;
    }

    const int reason = errno;
    ::close(opened);
    if (reason == EWOULDBLOCK || reason == EAGAIN) {
        error = "another bt-speaker-cli is already running -- it holds the lock on " + path;
        return PidFileStatus::AlreadyRunning;
    }
    error = std::string("cannot lock ") + what + " " + path + ": " + std::strerror(reason) +
            ". A " + what +
            " has to be on a local filesystem: flock is emulated over NFS and is not dependable "
            "over SMB";
    return PidFileStatus::Failed;
}

void release_lock(int fd) {
    ::flock(fd, LOCK_UN);
    ::close(fd);
}

PidFile::~PidFile() {
    if (this->fd_ < 0) {
        return;
    }
    ::unlink(this->path_.c_str());
    ::close(this->fd_);
}

PidFileStatus PidFile::acquire(const std::string& path, std::string& error) {
    int fd = -1;
    const PidFileStatus locked = lock_file(path, "pidfile", PIDFILE_MODE, fd, error);
    if (locked != PidFileStatus::Ok) {
        return locked;
    }

    if (::ftruncate(fd, 0) != 0) {
        error = "cannot truncate pidfile " + path + ": " + std::strerror(errno);
        ::close(fd);
        return PidFileStatus::Failed;
    }

    const std::string pid = std::to_string(static_cast<long>(::getpid())) + "\n";
    const ssize_t written = ::write(fd, pid.data(), pid.size());
    if (written != static_cast<ssize_t>(pid.size())) {
        error = "cannot write pidfile " + path + ": " +
                (written < 0 ? std::strerror(errno) : "short write");
        ::close(fd);
        return PidFileStatus::Failed;
    }

    this->path_ = path;
    this->fd_ = fd;
    return PidFileStatus::Ok;
}

PidFileStatus probe_pidfile(const std::string& path, std::string& error) {
    int fd = -1;
    const PidFileStatus locked = lock_file(path, "pidfile", PIDFILE_MODE, fd, error);
    if (locked == PidFileStatus::Ok) {
        release_lock(fd);
    }
    return locked;
}

bool daemonize(bool discard_stderr, std::string& error) {
    const pid_t child = ::fork();
    if (child < 0) {
        error = std::string("cannot fork: ") + std::strerror(errno);
        return false;
    }
    if (child > 0) {
        ::_exit(0);
    }

    if (::setsid() == static_cast<pid_t>(-1)) {
        error = std::string("cannot start a new session: ") + std::strerror(errno);
        return false;
    }

    if (::chdir("/") != 0) {
        error = std::string("cannot change directory to /: ") + std::strerror(errno);
        return false;
    }

    ::umask(S_IWGRP | S_IWOTH);

    const int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        error = std::string("cannot open /dev/null: ") + std::strerror(errno);
        return false;
    }
    const bool redirected = ::dup2(null_fd, STDIN_FILENO) >= 0 &&
                            ::dup2(null_fd, STDOUT_FILENO) >= 0 &&
                            (!discard_stderr || ::dup2(null_fd, STDERR_FILENO) >= 0);
    const int reason = errno;
    if (null_fd > STDERR_FILENO) {
        ::close(null_fd);
    }
    if (!redirected) {
        error = std::string("cannot redirect the standard streams: ") + std::strerror(reason);
        return false;
    }
    return true;
}

}  // namespace bt_speaker_cli
