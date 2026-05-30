// SocketUtil.h — small blocking-socket helpers. We use MSG_NOSIGNAL so a write
// to a closed peer returns EPIPE instead of killing the process with SIGPIPE.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <sys/socket.h>
#include <sys/time.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   // macOS: rely on SO_NOSIGPIPE / ignored SIGPIPE instead
#endif

namespace sec {
namespace netutil {

// Send the whole buffer; returns false on a real error or closed connection.
inline bool sendAll(int fd, const uint8_t* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        ssize_t n = ::send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

// Apply send/recv timeouts so a dead peer cannot wedge the session thread.
inline void setTimeouts(int fd, int seconds) {
    timeval tv{ seconds, 0 };
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

} // namespace netutil
} // namespace sec
