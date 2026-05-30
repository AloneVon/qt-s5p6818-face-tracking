// FrameHub.h — single-writer / multi-reader exchange of the latest frame.
// The vision thread publishes one immutable Frame per capture: the annotated
// BGR Mat (for the local display) AND a JPEG encoded once (for every client
// session, so N clients don't re-encode the same image). Readers copy the
// shared_ptr under a short lock, then read the immutable Frame lock-free. No
// queue => consumers always get the freshest frame; a slow client can't stall
// capture.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <opencv2/core.hpp>

namespace sec {

struct Frame {
    cv::Mat              mat;     // annotated BGR, for local display
    std::vector<uint8_t> jpeg;   // encoded once, for network clients
    uint32_t seq    = 0;
    int      width  = 0;
    int      height = 0;
    uint64_t tsMs   = 0;
};

class FrameHub {
public:
    void publish(std::shared_ptr<const Frame> f) {
        std::lock_guard<std::mutex> lk(m_);
        latest_ = std::move(f);
    }

    // Latest frame, or nullptr before the first publish.
    std::shared_ptr<const Frame> latest() const {
        std::lock_guard<std::mutex> lk(m_);
        return latest_;
    }

private:
    mutable std::mutex m_;
    std::shared_ptr<const Frame> latest_;
};

} // namespace sec
