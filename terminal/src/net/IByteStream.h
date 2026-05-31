// IByteStream.h — the raw byte transport that sits *below* SEC1/WebSocket
// framing. It hides whether the bytes on the wire are plaintext (a bare socket)
// or TLS-encrypted, so TcpConn / WebSocketConn can frame SEC1 the same way over
// either. This is the seam where "secure vs cleartext" lives: swapping a
// RawByteStream for a TlsByteStream turns TCP into TLS and ws into wss without
// touching a line of framing code above it.
//
// Contract (mirrors POSIX so the existing poll()-driven session keeps working):
//   read():  >0  bytes written to buf
//             0  peer closed (orderly)
//            <0  none available right now — inspect errno: EAGAIN/EWOULDBLOCK/
//                EINTR mean "retry later", anything else is fatal.
//   writeAll(): true once the whole buffer is sent, false on a real error.
// The stream owns the fd and closes it in close().
#pragma once

#include <cstddef>
#include <cstdint>

#include <sys/socket.h>
#include <unistd.h>

#include "SocketUtil.h"

namespace sec {

class IByteStream {
public:
    virtual ~IByteStream() = default;

    // Complete transport-level setup before any framing bytes flow. For TLS this
    // runs the SSL handshake (SSL_accept); for a raw socket it is a no-op.
    virtual bool handshake() = 0;

    virtual int  read(uint8_t* buf, std::size_t cap) = 0;        // see contract above
    virtual bool writeAll(const uint8_t* data, std::size_t len) = 0;

    virtual int  fd() const = 0;   // for poll(); -1 once closed
    virtual void close() = 0;
};

// Plaintext passthrough: exactly the ::recv / sendAll / ::close the transports
// used before this layer existed, so the cleartext path is byte-for-byte
// unchanged.
class RawByteStream : public IByteStream {
public:
    explicit RawByteStream(int fd) : fd_(fd) {}
    ~RawByteStream() override { close(); }

    bool handshake() override { return true; }  // nothing to negotiate

    int read(uint8_t* buf, std::size_t cap) override {
        return static_cast<int>(::recv(fd_, buf, cap, 0));  // errno preserved
    }
    bool writeAll(const uint8_t* data, std::size_t len) override {
        return netutil::sendAll(fd_, data, len);
    }

    int  fd() const override { return fd_; }
    void close() override {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_;
};

} // namespace sec
