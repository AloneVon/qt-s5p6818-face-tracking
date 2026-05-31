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
std::string headerValue(const std::string& req, const char* name) {
    std::string lower(req.size(), '\0');
    std::transform(req.begin(), req.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::size_t pos = lower.find(key + ":");
    if (pos == std::string::npos) return "";
    pos += key.size() + 1;                       // skip "name:"
    std::size_t end = req.find("\r\n", pos);
    if (end == std::string::npos) end = req.size();
    std::size_t a = req.find_first_not_of(" \t", pos);
    if (a == std::string::npos || a >= end) return "";
    std::size_t b = req.find_last_not_of(" \t", end - 1);
    return req.substr(a, b - a + 1);
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

    const std::string key = headerValue(req, "Sec-WebSocket-Key");
    if (key.empty()) {
        LOGW("WebSocketConn: missing Sec-WebSocket-Key");
        return false;
    }

    // Bytes after the header terminator are an early WebSocket frame; keep them.
    const std::size_t bodyStart = req.find("\r\n\r\n") + 4;
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
