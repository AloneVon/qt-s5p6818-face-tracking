// IDisplay.h — local display abstraction. Board uses Framebuffer (/dev/fb0);
// host-dev uses WindowDisplay (cv::imshow). show() takes a BGR frame and is
// responsible for fitting it onto the device.
#pragma once

#include <opencv2/core.hpp>

namespace sec {

class IDisplay {
public:
    virtual ~IDisplay() = default;
    virtual bool open() = 0;
    virtual void show(const cv::Mat& bgr) = 0;
    virtual void close() = 0;
};

} // namespace sec
