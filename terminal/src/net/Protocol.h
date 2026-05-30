// Protocol.h — wire protocol shared by the terminal and all clients.
// Header-only, no external deps, big-endian on the wire so it is portable to
// the Qt PC client (macOS/Linux/Windows) and decodable from JS (DataView).
//
//   Frame = Header(12 bytes, big-endian) + payload(length bytes)
//     magic  u32  0x53454331 ('S','E','C','1')
//     type   u16  see MsgType
//     flags  u16
//     length u32  number of payload bytes following the header
//
// VIDEO_FRAME payload = subheader(20 bytes) + JPEG:
//     seq u32, width u32, height u32, ts_ms u64, then JPEG bytes.
// Control messages (SERVO_CMD/MODE_CMD/FILE_*) carry a UTF-8 JSON payload.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace sec {
namespace proto {

constexpr uint32_t kMagic      = 0x53454331u;
constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kVideoSubheaderSize = 20;

enum class MsgType : uint16_t {
    Hello          = 0x0001,
    VideoFrame     = 0x0010,
    ServoCmd       = 0x0020,
    ModeCmd        = 0x0021,
    FileListReq    = 0x0030,
    FileListResp   = 0x0031,
    FileGetReq     = 0x0032,
    FileData       = 0x0033,
    FileDeleteReq  = 0x0034,
    FileDeleteResp = 0x0035,
    SnapshotReq    = 0x0040,
    Heartbeat      = 0x00F0,
};

struct Header {
    uint32_t magic  = kMagic;
    uint16_t type   = 0;
    uint16_t flags  = 0;
    uint32_t length = 0;
};

// ---- big-endian put helpers (append to a byte buffer) ---------------------
inline void putU16(std::vector<uint8_t>& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}
inline void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}
inline void putU64(std::vector<uint8_t>& b, uint64_t v) {
    for (int s = 56; s >= 0; s -= 8)
        b.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

// ---- big-endian get helpers (read from raw bytes) -------------------------
inline uint16_t getU16(const uint8_t* p) {
    return static_cast<uint16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
}
inline uint32_t getU32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
}
inline uint64_t getU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | uint64_t(p[i]);
    return v;
}

// Parse a 12-byte header. Returns false if too short or bad magic.
inline bool decodeHeader(const uint8_t* p, std::size_t n, Header& out) {
    if (n < kHeaderSize) return false;
    out.magic  = getU32(p);
    if (out.magic != kMagic) return false;
    out.type   = getU16(p + 4);
    out.flags  = getU16(p + 6);
    out.length = getU32(p + 8);
    return true;
}

// Build a complete message (header + payload) ready to send.
inline std::vector<uint8_t> buildMessage(MsgType type, const uint8_t* payload,
                                         std::size_t len, uint16_t flags = 0) {
    std::vector<uint8_t> b;
    b.reserve(kHeaderSize + len);
    putU32(b, kMagic);
    putU16(b, static_cast<uint16_t>(type));
    putU16(b, flags);
    putU32(b, static_cast<uint32_t>(len));
    if (payload && len) b.insert(b.end(), payload, payload + len);
    return b;
}

inline std::vector<uint8_t> buildMessage(MsgType type, const std::string& json,
                                         uint16_t flags = 0) {
    return buildMessage(type, reinterpret_cast<const uint8_t*>(json.data()),
                        json.size(), flags);
}

// Build a VIDEO_FRAME message: 20-byte subheader + JPEG bytes.
inline std::vector<uint8_t> buildVideoFrame(uint32_t seq, uint32_t w, uint32_t h,
                                            uint64_t tsMs, const uint8_t* jpeg,
                                            std::size_t jpegLen) {
    std::vector<uint8_t> sub;
    sub.reserve(kVideoSubheaderSize + jpegLen);
    putU32(sub, seq);
    putU32(sub, w);
    putU32(sub, h);
    putU64(sub, tsMs);
    if (jpeg && jpegLen) sub.insert(sub.end(), jpeg, jpeg + jpegLen);
    return buildMessage(MsgType::VideoFrame, sub.data(), sub.size());
}

} // namespace proto
} // namespace sec
