// WebSocketProto.h — pure, dependency-free RFC 6455 helpers (server side): the
// Sec-WebSocket-Accept hash, frame encoding (server→client, unmasked) and frame
// decoding (client→server, unmasking). No sockets here, so this is unit-testable
// off-board — see tools/test_ws_codec.cpp, which runs the RFC's own vectors.
//
// WebSocketConn.cpp adds the socket plumbing (handshake I/O, buffering,
// ping/pong) on top of these.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sec {
namespace ws {

// Appended to the client key before hashing, per RFC 6455 §1.3.
constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ---- SHA-1 (FIPS 180) -----------------------------------------------------
inline std::array<uint8_t, 20> sha1(const uint8_t* data, std::size_t len) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
             h3 = 0x10325476, h4 = 0xC3D2E1F0;

    auto rol = [](uint32_t v, int b) { return (v << b) | (v >> (32 - b)); };

    // Padded message length: original + 1 (0x80) + zeros + 8 (bit length),
    // rounded up to a multiple of 64.
    const uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int s = 56; s >= 0; s -= 8)
        msg.push_back(static_cast<uint8_t>((bitLen >> s) & 0xFF));

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(msg[off + i * 4]) << 24) |
                   (uint32_t(msg[off + i * 4 + 1]) << 16) |
                   (uint32_t(msg[off + i * 4 + 2]) << 8) |
                    uint32_t(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
            uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> out{};
    const uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
    }
    return out;
}

// ---- base64 (standard alphabet, padded) -----------------------------------
inline std::string base64(const uint8_t* data, std::size_t len) {
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 3 <= len; i += 3) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back(tbl[n & 0x3F]);
    }
    if (len - i == 1) {
        const uint32_t n = uint32_t(data[i]) << 16;
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (len - i == 2) {
        const uint32_t n = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back(tbl[(n >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

// Sec-WebSocket-Accept for a given Sec-WebSocket-Key (RFC 6455 §4.2.2).
inline std::string acceptKey(const std::string& clientKey) {
    std::string s = clientKey + kGuid;
    auto digest = sha1(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    return base64(digest.data(), digest.size());
}

// ---- framing --------------------------------------------------------------
// Opcodes we care about.
enum : uint8_t {
    kOpContinuation = 0x0,
    kOpText         = 0x1,
    kOpBinary       = 0x2,
    kOpClose        = 0x8,
    kOpPing         = 0x9,
    kOpPong         = 0xA,
};

// Encode one server→client frame (FIN set, never masked) and append to `out`.
inline void encodeFrame(std::vector<uint8_t>& out, uint8_t opcode,
                        const uint8_t* data, std::size_t len) {
    out.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0F)));
    if (len < 126) {
        out.push_back(static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        out.push_back(126);
        out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        out.push_back(127);
        for (int s = 56; s >= 0; s -= 8)
            out.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> s) & 0xFF));
    }
    if (data && len) out.insert(out.end(), data, data + len);
}

enum class Parse { Ok, NeedMore, Error };

struct Frame {
    uint8_t opcode = 0;
    bool fin = true;
    std::vector<uint8_t> payload;
};

// Try to decode one client→server frame from p[0..n). On Ok, `consumed` is the
// number of bytes the frame occupied and `out` holds the unmasked payload.
// NeedMore => buffer the rest and retry; Error => protocol violation / too big.
//
// `requireMasked` defaults to true (RFC 6455 §5.1: client→server frames MUST be
// masked, server MUST close on an unmasked one). The off-board codec test toggles
// this off to exercise the encode→decode symmetry with the server-side encoder.
inline Parse decodeFrame(const uint8_t* p, std::size_t n, std::size_t& consumed,
                         Frame& out, std::size_t maxPayload = 16u * 1024u * 1024u,
                         bool requireMasked = true) {
    if (n < 2) return Parse::NeedMore;
    const uint8_t b0 = p[0], b1 = p[1];
    out.fin = (b0 & 0x80) != 0;
    out.opcode = b0 & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    std::size_t i = 2;
    if (len == 126) {
        if (n < 4) return Parse::NeedMore;
        len = (uint64_t(p[2]) << 8) | p[3];
        i = 4;
    } else if (len == 127) {
        if (n < 10) return Parse::NeedMore;
        len = 0;
        for (int k = 0; k < 8; ++k) len = (len << 8) | p[2 + k];
        i = 10;
    }
    if (len > maxPayload) return Parse::Error;
    // Control frames (opcode high bit set) must be FIN=1 and ≤ 125 bytes
    // (RFC 6455 §5.5). A fragmented or oversize ping/pong/close is a protocol
    // violation — refuse before we ever allocate `out.payload`.
    if ((out.opcode & 0x8) && (!out.fin || len > 125)) return Parse::Error;
    // Client→server frames must be masked. The mock terminal and off-board test
    // can opt out (requireMasked=false) when exercising the codec end-to-end.
    if (requireMasked && !masked) return Parse::Error;
    const uint8_t* mask = nullptr;
    if (masked) {
        if (n < i + 4) return Parse::NeedMore;
        mask = p + i;
        i += 4;
    }
    if (n < i + len) return Parse::NeedMore;
    out.payload.resize(static_cast<std::size_t>(len));
    if (masked) {
        for (uint64_t k = 0; k < len; ++k) out.payload[k] = p[i + k] ^ mask[k & 3];
    } else if (len) {
        std::memcpy(out.payload.data(), p + i, static_cast<std::size_t>(len));
    }
    consumed = i + static_cast<std::size_t>(len);
    return Parse::Ok;
}

} // namespace ws
} // namespace sec
