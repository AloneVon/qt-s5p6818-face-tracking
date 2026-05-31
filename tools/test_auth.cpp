// test_auth.cpp — off-board unit test for terminal/src/net/Auth.h. Auth.h and the
// MiniJson.h it builds on are pure/header-only (no OpenCV, no sockets), so the
// security-critical token check is testable here on macOS even though
// ClientSession itself can't build (it pulls OpenCV via FrameHub/FileManager).
//
//   c++ -std=c++17 -I terminal/src tools/test_auth.cpp -o /tmp/test_auth && /tmp/test_auth
#include <cstdio>
#include <string>

#include "net/Auth.h"

using namespace sec;

static int failures = 0;
static void check(const char* name, bool ok) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

int main() {
    // ---- constantTimeEqual --------------------------------------------------
    check("equal tokens compare equal",
          auth::constantTimeEqual("s3cr3t-token", "s3cr3t-token"));
    check("same-length mismatch fails",
          !auth::constantTimeEqual("s3cr3t-token", "s3cr3t-tokeZ"));
    check("shorter supplied value fails",
          !auth::constantTimeEqual("s3cr3t-token", "s3cr3t"));
    check("longer supplied value fails",
          !auth::constantTimeEqual("s3cr3t", "s3cr3t-token"));
    check("empty vs empty is equal", auth::constantTimeEqual("", ""));
    check("empty expected vs non-empty fails",
          !auth::constantTimeEqual("", "anything"));
    check("non-empty expected vs empty fails",
          !auth::constantTimeEqual("token", ""));
    // Length is folded in as a boolean, not XOR-of-sizes, so 0 vs 256 (which
    // alias to 0 mod 256) must still fail.
    check("length 0 vs 256 fails (no mod-256 alias)",
          !auth::constantTimeEqual("", std::string(256, 'a')));

    // ---- helloAuthorized ----------------------------------------------------
    const std::string expected = "letmein";
    check("hello with matching token authorizes",
          auth::helloAuthorized(
              expected, "{\"role\":\"mobile\",\"ver\":1,\"token\":\"letmein\"}"));
    check("hello with wrong token rejected",
          !auth::helloAuthorized(
              expected, "{\"role\":\"mobile\",\"ver\":1,\"token\":\"nope\"}"));
    check("hello missing token rejected",
          !auth::helloAuthorized(expected, "{\"role\":\"mobile\",\"ver\":1}"));
    check("token found regardless of key order",
          auth::helloAuthorized(
              expected, "{\"token\":\"letmein\",\"role\":\"pc\",\"ver\":1}"));
    check("auth disabled (empty expected) accepts a token-less hello",
          auth::helloAuthorized("", "{\"role\":\"pc\",\"ver\":1}"));

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
