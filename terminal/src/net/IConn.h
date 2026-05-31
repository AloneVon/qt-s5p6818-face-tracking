// IConn.h — the byte transport a ClientSession runs on. It hides whether a
// client speaks raw TCP or a WebSocket so the session logic (SEC1 framing, file
// and control handling) is written once and reused for both.
//
// Contract: callers send and receive a clean SEC1 byte stream. recv() mirrors
// POSIX ::recv on a timed socket so the existing session loop keeps working:
//   >0  bytes written to buf
//    0  peer closed (orderly)
//   <0  none available right now — inspect errno: EAGAIN/EWOULDBLOCK/EINTR mean
//       "retry later", anything else is fatal.
// Each send() call carries exactly one complete SEC1 frame; a WebSocket
// transport therefore maps it to exactly one binary message.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sec {

class IConn {
public:
    virtual ~IConn() = default;

    // Underlying fd, for poll() and socket options. -1 once closed.
    virtual int fd() const = 0;

    // Complete any connection setup (e.g. the WebSocket HTTP upgrade) on the
    // session thread, before the first SEC1 byte is sent. true on success.
    virtual bool handshake() = 0;

    virtual int  recv(uint8_t* buf, std::size_t cap) = 0;        // see contract above
    virtual bool send(const uint8_t* data, std::size_t len) = 0; // one SEC1 frame
    virtual void close() = 0;
};

} // namespace sec
