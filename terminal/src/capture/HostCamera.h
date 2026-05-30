// HostCamera.h — DEV_HOST camera: wraps cv::VideoCapture so the pipeline can be
// developed on a desktop with a USB webcam (no V4L2 ioctl plumbing needed).
#pragma once

#include <opencv2/videoio.hpp>
#include "ICamera.h"

namespace sec {

class HostCamera : public ICamera {
public:
    HostCamera(int index, int width, int height, int fps);

    bool open() override;
    bool grab(cv::Mat& outBgr) override;
    void close() override;
    int width() const override  { return width_; }
    int height() const override { return height_; }

private:
    int index_;
    int width_;
    int height_;
    int fps_;
    cv::VideoCapture cap_;
};

} // namespace sec
