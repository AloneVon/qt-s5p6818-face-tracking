#include "WebSocketConn.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>

#include "WebSocketProto.h"
#include "../core/Log.h"

namespace sec {

namespace {

// Case-insensitive lookup of an HTTP header value (header names are
// case-insensitive per RFC 7230; the value is returned with its original case,
// which matters for the base64 Sec-WebSocket-Key). "" if absent.
//
// Anchored to start-of-line: only the "\r\n<name>:" form matches, so a header
// value containing the literal "Sec-WebSocket-Key:" can't masquerade as the
// header itself. `headerBlock` must be just the header section (no trailing
// body bytes) — the caller strips the body before calling this.
std::string headerValue(const std::string& headerBlock, const char* name) {
    std::string lower(headerBlock.size(), '\0');
    std::transform(headerBlock.begin(), headerBlock.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Each header sits at the start of a line. The request line itself doesn't
    // begin with "\r\n", so probe the very start as a synthetic line start too.
    const std::string needle = "\r\n" + key + ":";
    std::size_t pos = (lower.compare(0, key.size() + 1, key + ":") == 0)
                          ? 0
                          : lower.find(needle);
    if (pos == std::string::npos) return "";
    if (pos != 0) pos += 2;                       // skip the leading "\r\n"
    pos += key.size() + 1;                        // skip "name:"
    std::size_t end = headerBlock.find("\r\n", pos);
    if (end == std::string::npos) end = headerBlock.size();
    std::size_t a = headerBlock.find_first_not_of(" \t", pos);
    if (a == std::string::npos || a >= end) return "";
    std::size_t b = headerBlock.find_last_not_of(" \t", end - 1);
    return headerBlock.substr(a, b - a + 1);
}

} // namespace

bool WebSocketConn::handshake() {
    // Below us the byte stream negotiates first: TLS does its SSL_accept here
    // (no-op for a raw socket), so the HTTP upgrade rides the encrypted channel.
    if (!stream_->handshake()) return false;

    // Read the HTTP upgrade request up to the blank-line terminator. A read
    // timeout (set by the caller before handshake) bounds a slow/idle client.
    std::string req;
    uint8_t tmp[2048];
    while (req.find("\r\n\r\n") == std::string::npos) {
        if (req.size() > 16u * 1024u) {           // header flood guard
            LOGW("WebSocketConn: handshake headers too large");
            return false;
        }
        int n = stream_->read(tmp, sizeof(tmp));
        if (n > 0) { req.append(reinterpret_cast<char*>(tmp), static_cast<std::size_t>(n)); continue; }
        if (n == 0) return false;                 // peer closed mid-handshake
        if (errno == EINTR) continue;
        return false;                             // timeout / error
    }

    // Split header block from any early WebSocket frame bytes that piggybacked
    // on the same read. Search only the header block so frame bytes can't be
    // confused for an HTTP header.
    const std::size_t bodyStart = req.find("\r\n\r\n") + 4;
    const std::string headerBlock = req.substr(0, bodyStart - 2);  // keep trailing CRLF
    const std::string key = headerValue(headerBlock, "Sec-WebSocket-Key");
    if (key.empty()) {
        LOGW("WebSocketConn: missing Sec-WebSocket-Key");
        return false;
    }
    if (bodyStart < req.size())
        rx_.insert(rx_.end(), req.begin() + static_cast<std::ptrdiff_t>(bodyStart), req.end());

    const std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + ws::acceptKey(key) + "\r\n\r\n";
    if (!stream_->writeAll(reinterpret_cast<const uint8_t*>(resp.data()), resp.size()))
        return false;

    LOGI("WebSocketConn: upgraded");
    return true;
}

int WebSocketConn::recv(uint8_t* buf, std::size_t cap) {
    // Serve anything already de-framed before touching the socket.
    if (!app_.empty()) {
        const std::size_t k = std::min(cap, app_.size());
        std::memcpy(buf, app_.data(), k);
        app_.erase(app_.begin(), app_.begin() + static_cast<std::ptrdiff_t>(k));
        return static_cast<int>(k);
    }
    if (closed_) return 0;                         // close already processed

    uint8_t tmp[8192];
    int n = stream_->read(tmp, sizeof(tmp));
    if (n == 0) return 0;                          // orderly shutdown
    if (n < 0) return -1;                          // errno preserved for the caller
    rx_.insert(rx_.end(), tmp, tmp + n);

    drainFrames();                                 // → app_, answers ping, notes close

    if (!app_.empty()) {
        const std::size_t k = std::min(cap, app_.size());
        std::memcpy(buf, app_.data(), k);
        app_.erase(app_.begin(), app_.begin() + static_cast<std::ptrdiff_t>(k));
        return static_cast<int>(k);
    }
    if (closed_) return 0;                          // close frame, no app bytes
    errno = EAGAIN;                                // only control/partial bytes: retry later
    return -1;
}

void WebSocketConn::drainFrames() {
    std::size_t off = 0;
    while (true) {
        std::size_t consumed = 0;
        ws::Frame fr;
        const ws::Parse r =
            ws::decodeFrame(rx_.data() + off, rx_.size() - off, consumed, fr);
        if (r == ws::Parse::NeedMore) break;
        if (r == ws::Parse::Error) { closed_ = true; break; }
        off += consumed;

        switch (fr.opcode) {
            case ws::kOpContinuation:
            case ws::kOpText:
            case ws::kOpBinary:
                app_.insert(app_.end(), fr.payload.begin(), fr.payload.end());
                break;
            case ws::kOpPing:
                sendControl(ws::kOpPong, fr.payload.data(), fr.payload.size());
                break;
            case ws::kOpPong:
                break;
            case ws::kOpClose:
                sendControl(ws::kOpClose, fr.payload.data(), fr.payload.size());
                closed_ = true;
                break;
            default:
                break;
        }
        if (closed_) break;
    }
    if (off) rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(off));
}

bool WebSocketConn::send(const uint8_t* data, std::size_t len) {
    std::vector<uint8_t> frame;
    ws::encodeFrame(frame, ws::kOpBinary, data, len);   // one SEC1 frame → one message
    return stream_->writeAll(frame.data(), frame.size());
}

bool WebSocketConn::sendControl(uint8_t opcode, const uint8_t* data, std::size_t len) {
    std::vector<uint8_t> frame;
    ws::encodeFrame(frame, opcode, data, len);
    return stream_->writeAll(frame.data(), frame.size());
}

void WebSocketConn::close() {
    stream_->close();
}

} // namespace sec
