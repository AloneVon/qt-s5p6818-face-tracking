// Time.h — monotonic millisecond clock used for FPS, debounces, frame stamps.
#pragma once
#include <chrono>
#include <cstdint>

namespace sec {

inline uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace sec
