// test_ws_conn.cpp — off-board integration test for terminal/src/net/WebSocketConn.
//
// The full terminal can't build on macOS (OpenCV/V4L2/framebuffer), but
// WebSocketConn depends only on POSIX sockets + the pure WebSocketProto.h, so we
// can drive it end-to-end over a socketpair: feed it a real HTTP upgrade and
// masked client frames on one end, and check the SEC1 bytes / pong / close it
// produces on the other. This covers the parts test_ws_codec.cpp can't: the
// handshake, the de-framing state machine, ping→pong, the EAGAIN-on-partial
// contract, and close handling.
//
//   c++ -std=c++17 -I terminal/src tools/test_ws_conn.cpp \
//       terminal/src/net/WebSocketConn.cpp -o /tmp/test_ws_conn && /tmp/test_ws_conn
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "net/WebSocketConn.h"
#include "net/WebSocketProto.h"

using namespace sec;

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

// Build a masked client→server frame, exactly as a browser would put it on the
// wire (the inverse of ws::decodeFrame).
static std::vector<uint8_t> clientFrame(uint8_t opcode,
                                        const std::vector<uint8_t>& payload,
                                        bool fin = true) {
    std::vector<uint8_t> f;
    f.push_back(static_cast<uint8_t>((fin ? 0x80 : 0x00) | (opcode & 0x0F)));
    const uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    const std::size_t len = payload.size();
    if (len < 126) {
        f.push_back(static_cast<uint8_t>(0x80 | len));
    } else if (len <= 0xFFFF) {
        f.push_back(0x80 | 126);
        f.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        f.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        f.push_back(0x80 | 127);
        for (int s = 56; s >= 0; s -= 8)
            f.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> s) & 0xFF));
    }
    f.insert(f.end(), mask, mask + 4);
    for (std::size_t i = 0; i < len; ++i)
        f.push_back(static_cast<uint8_t>(payload[i] ^ mask[i & 3]));
    return f;
}

static std::vector<uint8_t> bytes(const char* s) {
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(s),
                                reinterpret_cast<const uint8_t*>(s) + std::strlen(s));
}

static void writeAll(int fd, const std::vector<uint8_t>& v) {
    std::size_t off = 0;
    while (off < v.size()) {
        ssize_t n = ::write(fd, v.data() + off, v.size() - off);
        if (n <= 0) return;
        off += static_cast<std::size_t>(n);
    }
}

int main() {
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        std::perror("socketpair");
        return 2;
    }
    const int client = sv[1];                 // we play the browser on this end
    // Don't let a logic bug hang the test forever.
    timeval tv{2, 0};
    ::setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    WebSocketConn conn(sv[0]);                // class under test owns sv[0]

    // 1) Handshake: send a real upgrade request, expect 101 + correct accept key
    //    (RFC 6455 §1.3 example key/accept).
    {
        const std::string req =
            "GET /ws HTTP/1.1\r\n"
            "Host: board.local:8889\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        writeAll(client, bytes(req.c_str()));
        const bool ok = conn.handshake();

        char resp[512] = {0};
        ssize_t rn = ::read(client, resp, sizeof(resp) - 1);
        const std::string r(resp, rn > 0 ? static_cast<std::size_t>(rn) : 0);
        check("handshake returns true", ok);
        check("response is 101 Switching Protocols",
              r.find("HTTP/1.1 101") != std::string::npos);
        check("accept key matches RFC example",
              r.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") !=
                  std::string::npos);
    }

    // 2) A masked binary message de-frames to its raw payload.
    {
        writeAll(client, clientFrame(ws::kOpBinary, bytes("SEC1-payload")));
        uint8_t buf[256];
        int n = conn.recv(buf, sizeof(buf));
        check("recv returns the de-framed payload",
              n == 12 && std::memcmp(buf, "SEC1-payload", 12) == 0);
    }

    // 3) Two messages arriving together both surface (one read, concatenated).
    {
        auto two = clientFrame(ws::kOpBinary, bytes("AAA"));
        auto more = clientFrame(ws::kOpBinary, bytes("BBBB"));
        two.insert(two.end(), more.begin(), more.end());
        writeAll(client, two);
        uint8_t buf[256];
        int n = conn.recv(buf, sizeof(buf));
        check("two coalesced frames concatenate",
              n == 7 && std::memcmp(buf, "AAABBBB", 7) == 0);
    }

    // 4) A ping is answered with a pong (same payload) and yields no app bytes.
    {
        writeAll(client, clientFrame(ws::kOpPing, bytes("hi")));
        uint8_t buf[64];
        errno = 0;
        int n = conn.recv(buf, sizeof(buf));
        check("ping produces no app data (EAGAIN)", n < 0 && errno == EAGAIN);

        uint8_t resp[64];
        ssize_t rn = ::read(client, resp, sizeof(resp));
        check("server sent a pong with the ping payload",
              rn == 4 && resp[0] == (0x80 | ws::kOpPong) && resp[1] == 2 &&
                  resp[2] == 'h' && resp[3] == 'i');
    }

    // 5) A frame split across two writes: first half => EAGAIN, then completes.
    {
        auto f = clientFrame(ws::kOpBinary, bytes("split"));
        std::vector<uint8_t> head(f.begin(), f.begin() + 3);
        std::vector<uint8_t> tail(f.begin() + 3, f.end());
        writeAll(client, head);
        uint8_t buf[64];
        errno = 0;
        int n1 = conn.recv(buf, sizeof(buf));
        check("partial frame => EAGAIN", n1 < 0 && errno == EAGAIN);
        writeAll(client, tail);
        int n2 = conn.recv(buf, sizeof(buf));
        check("remainder completes the frame",
              n2 == 5 && std::memcmp(buf, "split", 5) == 0);
    }

    // 6) A close frame makes recv report EOF (and the server echoes a close).
    {
        writeAll(client, clientFrame(ws::kOpClose, {}));
        uint8_t buf[64];
        int n = conn.recv(buf, sizeof(buf));
        check("close frame => recv returns 0 (EOF)", n == 0);
        uint8_t resp[64];
        ssize_t rn = ::read(client, resp, sizeof(resp));
        check("server echoed a close frame",
              rn >= 2 && resp[0] == (0x80 | ws::kOpClose));
    }

    conn.close();
    ::close(client);

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
