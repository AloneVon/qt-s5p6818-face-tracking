// Types.h — tiny dependency-free geometry/enum types shared across threads.
// Keeping these out of OpenCV lets the shared state headers stay lightweight.
#pragma once

namespace sec {

struct Point2i { int x = 0; int y = 0; };

struct Recti {
    int x = 0, y = 0, w = 0, h = 0;
    Point2i center() const { return { x + w / 2, y + h / 2 }; }
    bool empty() const { return w <= 0 || h <= 0; }
};

enum class TrackMode { Auto, Manual };

} // namespace sec
