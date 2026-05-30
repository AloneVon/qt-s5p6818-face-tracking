// TrackingState.h — detection result shared from the vision thread to the
// business (servo) thread and the display thread. Written once per processed
// frame; read at the servo control rate. Small struct under a mutex is plenty.
#pragma once

#include <mutex>
#include "Types.h"

namespace sec {

class TrackingState {
public:
    struct Snapshot {
        bool    found = false;
        Recti   box;        // face bounding box at full capture resolution
        Point2i center;     // box center (frame coords)
        int     frameW = 0; // capture frame size, so consumers can compute error
        int     frameH = 0;
    };

    void update(bool found, const Recti& box, int frameW, int frameH) {
        std::lock_guard<std::mutex> lk(m_);
        s_.found  = found;
        s_.box    = box;
        s_.center = box.center();
        s_.frameW = frameW;
        s_.frameH = frameH;
    }

    Snapshot get() const {
        std::lock_guard<std::mutex> lk(m_);
        return s_;
    }

private:
    mutable std::mutex m_;
    Snapshot s_;
};

} // namespace sec
