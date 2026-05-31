// TcpConn.h — IConn for a client that speaks SEC1 straight on the byte stream
// (no extra framing). It is a thin passthrough to its IByteStream, so the same
// class serves both plaintext TCP (RawByteStream) and TLS (TlsByteStream); the
// transport decides which stream it is handed.
#pragma once

#include <memory>
#include <utility>

#include "IByteStream.h"
#include "IConn.h"

namespace sec {

class TcpConn : public IConn {
public:
    explicit TcpConn(std::unique_ptr<IByteStream> stream)
        : stream_(std::move(stream)) {}
    ~TcpConn() override { close(); }

    int  fd() const override { return stream_->fd(); }
    bool handshake() override { return stream_->handshake(); }  // TLS accept, or no-op

    int recv(uint8_t* buf, std::size_t cap) override {
        return stream_->read(buf, cap);          // errno preserved for the caller
    }
    bool send(const uint8_t* data, std::size_t len) override {
        return stream_->writeAll(data, len);
    }
    void close() override { stream_->close(); }

private:
    std::unique_ptr<IByteStream> stream_;
};

} // namespace sec
