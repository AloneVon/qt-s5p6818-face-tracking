// StatusBoard.h — small scalar status written by worker threads and read by the
// display thread (and client HELLO/heartbeats). Plain atomics, no locking.
#pragma once

#include <atomic>

namespace sec {

struct StatusBoard {
    std::atomic<double> panAngle{90.0};
    std::atomic<double> tiltAngle{90.0};
    std::atomic<bool>   autoTrack{true};
    std::atomic<double> captureFps{0.0};
    std::atomic<bool>   faceFound{false};
    std::atomic<int>    clientCount{0};
};

} // namespace sec
