// ICamera.h — camera abstraction. Board uses V4L2Camera; host-dev uses
// HostCamera (cv::VideoCapture). grab() always yields a BGR cv::Mat.
#pragma once

#include <opencv2/core.hpp>

namespace sec {

class ICamera {
public:
    virtual ~ICamera() = default;
    virtual bool open() = 0;
    // Blocks for the next frame; fills outBgr (CV_8UC3). False on error.
    virtual bool grab(cv::Mat& outBgr) = 0;
    virtual void close() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};

} // namespace sec
