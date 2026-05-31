// TcpConn.h — IConn over a plain TCP socket: a 1:1 passthrough of the raw
// ::recv / sendAll / ::close calls the session used before the transport was
// abstracted, so the TCP path behaves exactly as it always has.
#pragma once

#include <sys/socket.h>
#include <unistd.h>

#include "IConn.h"
#include "SocketUtil.h"

namespace sec {

class TcpConn : public IConn {
public:
    explicit TcpConn(int fd) : fd_(fd) {}
    ~TcpConn() override { close(); }

    int  fd() const override { return fd_; }
    bool handshake() override { return true; }  // nothing to negotiate

    int recv(uint8_t* buf, std::size_t cap) override {
        return static_cast<int>(::recv(fd_, buf, cap, 0));  // errno preserved for the caller
    }
    bool send(const uint8_t* data, std::size_t len) override {
        return netutil::sendAll(fd_, data, len);
    }
    void close() override {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_;
};

} // namespace sec
