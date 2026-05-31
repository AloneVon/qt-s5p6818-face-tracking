// test_ws_codec.cpp — off-board unit test for terminal/src/net/WebSocketProto.h.
//
// The terminal itself can't build on macOS (OpenCV / V4L2 / framebuffer), but
// the WebSocket codec is pure and portable, so we verify the fiddly parts — the
// accept-key hash and frame (de)framing — against RFC 6455's own examples.
//
//   c++ -std=c++17 -I terminal/src tools/test_ws_codec.cpp -o /tmp/test_ws && /tmp/test_ws
#include <cstdio>
#include <string>
#include <vector>

#include "net/WebSocketProto.h"

using namespace sec;

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

static std::string hex(const std::vector<uint8_t>& v) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : v) { s.push_back(d[b >> 4]); s.push_back(d[b & 0xF]); }
    return s;
}

int main() {
    // 1) base64 of a known string (RFC 4648).
    check("base64('Hello')",
          ws::base64(reinterpret_cast<const uint8_t*>("Hello"), 5) == "SGVsbG8=");

    // 2) Sec-WebSocket-Accept, RFC 6455 §1.3 worked example.
    check("acceptKey RFC6455 example",
          ws::acceptKey("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    // 3) Decode a single-frame masked "Hello" (RFC 6455 §5.7).
    {
        const uint8_t f[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d,
                             0x7f, 0x9f, 0x4d, 0x51, 0x58};
        std::size_t consumed = 0;
        ws::Frame fr;
        auto r = ws::decodeFrame(f, sizeof(f), consumed, fr);
        const std::string payload(fr.payload.begin(), fr.payload.end());
        check("decode masked 'Hello'",
              r == ws::Parse::Ok && consumed == sizeof(f) && fr.fin &&
              fr.opcode == ws::kOpText && payload == "Hello");
    }

    // 4) Encode a server frame for "Hello" — must equal the unmasked example
    //    from RFC 6455 §5.7: 0x81 0x05 'H' 'e' 'l' 'l' 'o'.
    {
        std::vector<uint8_t> out;
        ws::encodeFrame(out, ws::kOpText, reinterpret_cast<const uint8_t*>("Hello"), 5);
        check("encode server 'Hello'", hex(out) == "8105" "48656c6c6f");
    }

    // 5) Length boundaries: 125 (7-bit), 126 (16-bit header), 65536 (64-bit).
    {
        std::vector<uint8_t> a, b, c;
        std::vector<uint8_t> p125(125, 0xAB), p126(126, 0xCD), p64k(65536, 0xEE);
        ws::encodeFrame(a, ws::kOpBinary, p125.data(), p125.size());
        ws::encodeFrame(b, ws::kOpBinary, p126.data(), p126.size());
        ws::encodeFrame(c, ws::kOpBinary, p64k.data(), p64k.size());
        check("len 125 uses 1-byte length", a[1] == 125 && a.size() == 2 + 125);
        check("len 126 uses 16-bit length",
              b[1] == 126 && b[2] == 0x00 && b[3] == 0x7E && b.size() == 4 + 126);
        check("len 65536 uses 64-bit length",  // 0x0000000000010000, big-endian
              c[1] == 127 && c[2] == 0 && c[6] == 0x00 && c[7] == 0x01 &&
              c[8] == 0x00 && c[9] == 0x00 && c.size() == 10 + 65536);
    }

    // 6) Round-trip: encode (server, unmasked) then decode it back.
    {
        const char* msg = "SEC1-over-websocket round trip \x01\x02\x03";
        const std::size_t n = std::strlen(msg);
        std::vector<uint8_t> wire;
        ws::encodeFrame(wire, ws::kOpBinary, reinterpret_cast<const uint8_t*>(msg), n);
        std::size_t consumed = 0;
        ws::Frame fr;
        auto r = ws::decodeFrame(wire.data(), wire.size(), consumed, fr);
        check("encode→decode round trip",
              r == ws::Parse::Ok && consumed == wire.size() &&
              fr.opcode == ws::kOpBinary && fr.payload.size() == n &&
              std::memcmp(fr.payload.data(), msg, n) == 0);
    }

    // 7) Partial buffers report NeedMore (not Error) so the caller waits.
    {
        const uint8_t f[] = {0x82, 0x83, 0x00, 0x00, 0x00, 0x00, 0x41};  // says 3 bytes, only 1
        std::size_t consumed = 0;
        ws::Frame fr;
        check("partial frame => NeedMore",
              ws::decodeFrame(f, sizeof(f), consumed, fr) == ws::Parse::NeedMore);
        check("2-byte header alone => NeedMore",
              ws::decodeFrame(f, 1, consumed, fr) == ws::Parse::NeedMore);
    }

    // 8) Oversize length is rejected.
    {
        const uint8_t f[] = {0x82, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        std::size_t consumed = 0;
        ws::Frame fr;
        check("oversize payload => Error",
              ws::decodeFrame(f, sizeof(f), consumed, fr, 1024) == ws::Parse::Error);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
