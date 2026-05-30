// V4L2Camera.h — raw V4L2 capture via ioctl + mmap (the educational core of
// the camera path). Negotiates MJPEG (preferred) or YUYV, streams with a small
// ring of mmap'd buffers, and decodes each frame to BGR for OpenCV.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ICamera.h"

namespace sec {

class V4L2Camera : public ICamera {
public:
    V4L2Camera(std::string device, int width, int height, int fps);
    ~V4L2Camera() override;

    bool open() override;
    bool grab(cv::Mat& outBgr) override;
    void close() override;
    int width() const override  { return width_; }
    int height() const override { return height_; }

private:
    struct MmapBuffer { void* start = nullptr; std::size_t length = 0; };

    bool initFormat();
    bool initBuffers();
    bool startStreaming();

    std::string device_;
    int width_;
    int height_;
    int fps_;
    int fd_ = -1;
    uint32_t pixelFormat_ = 0;   // V4L2_PIX_FMT_MJPEG or V4L2_PIX_FMT_YUYV
    std::vector<MmapBuffer> buffers_;
    bool streaming_ = false;
};

} // namespace sec
