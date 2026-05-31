// WebSocketConn.h — IConn over an RFC 6455 WebSocket, so a browser/Capacitor
// WebView (which cannot open a raw TCP socket) can still speak SEC1 to the
// terminal. The codec lives in WebSocketProto.h; this class adds the socket
// plumbing: the HTTP Upgrade handshake, inbound de-framing, and outbound
// framing.
//
// The session above us reads and writes a clean SEC1 byte stream (see IConn):
//   - recv() pulls socket bytes, de-frames the client's (masked) messages, and
//     returns their concatenated payloads. Ping/pong/close are handled here and
//     never surface to the session. "Nothing decodable yet" maps to errno=EAGAIN
//     so the session's existing retry path applies.
//   - send() wraps each complete SEC1 frame the session hands us in exactly one
//     binary WebSocket message — the "one frame per message" guarantee the
//     mobile client relies on.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "IByteStream.h"
#include "IConn.h"

namespace sec {

class WebSocketConn : public IConn {
public:
    explicit WebSocketConn(std::unique_ptr<IByteStream> stream)
        : stream_(std::move(stream)) {}
    ~WebSocketConn() override { close(); }

    int  fd() const override { return stream_->fd(); }
    bool handshake() override;                              // TLS accept → HTTP Upgrade → 101
    int  recv(uint8_t* buf, std::size_t cap) override;      // SEC1 bytes; see IConn
    bool send(const uint8_t* data, std::size_t len) override;  // one binary message
    void close() override;

private:
    // Frame `data` with `opcode` (unmasked, server→client) and write it whole.
    bool sendControl(uint8_t opcode, const uint8_t* data, std::size_t len);
    // De-frame as many complete messages as rx_ holds: stage data payloads into
    // app_, answer pings, and note a close. Leaves any partial frame in rx_.
    void drainFrames();

    std::unique_ptr<IByteStream> stream_;  // raw socket or TLS, below the WS framing
    bool closed_ = false;        // peer close seen/sent — recv() now reports EOF
    std::vector<uint8_t> rx_;    // raw socket bytes not yet de-framed
    std::vector<uint8_t> app_;   // de-framed SEC1 bytes awaiting recv()
};

} // namespace sec
