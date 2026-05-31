// Auth.h — shared-token gate for client connections. Pure and header-only (no
// OpenCV, no sockets), so it compiles and is unit-testable off-board even though
// ClientSession itself can't build on macOS. The terminal requires every client
// to present a matching token in its HELLO before any video or control is
// exchanged; an empty configured token disables the check (open LAN — the
// original behaviour, kept for mocks and back-compat).
#pragma once

#include <cstddef>
#include <string>

#include "MiniJson.h"

namespace sec {
namespace auth {

// Content-constant-time comparison: never returns early on the first differing
// byte, so a network attacker can't recover the secret one byte at a time via
// response timing. The scan runs exactly `expected.size()` iterations — a value
// fixed per deployment — so timing leaks neither the secret's content nor the
// supplied value's length.
inline bool constantTimeEqual(const std::string& expected, const std::string& got) {
    const std::size_t n = expected.size();
    unsigned char diff = (n == got.size()) ? 0 : 1;  // length mismatch => fail
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char g =
            got.empty() ? 0 : static_cast<unsigned char>(got[i % got.size()]);
        diff |= static_cast<unsigned char>(
            static_cast<unsigned char>(expected[i]) ^ g);
    }
    return diff == 0;
}

// True if `helloBody` (the JSON payload of a HELLO message) carries a "token"
// string equal to `expected`. A missing token compares against "" and so fails
// whenever a non-empty token is required.
inline bool helloAuthorized(const std::string& expected, const std::string& helloBody) {
    std::string token;
    mjson::getString(helloBody, "token", token);  // token stays "" if absent
    return constantTimeEqual(expected, token);
}

} // namespace auth
} // namespace sec
