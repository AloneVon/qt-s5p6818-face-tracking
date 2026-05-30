// Framebuffer.h — direct /dev/fb0 output. Reads the screen geometry/pixel
// format at runtime, mmaps the framebuffer, and blits BGR frames (converted to
// the device's RGB565 or 32bpp BGRA layout), centered on screen.
#pragma once

#include <cstdint>
#include <string>

#include "IDisplay.h"

namespace sec {

class Framebuffer : public IDisplay {
public:
    explicit Framebuffer(std::string device);
    ~Framebuffer() override;

    bool open() override;
    void show(const cv::Mat& bgr) override;
    void close() override;

private:
    std::string device_;
    int      fd_ = -1;
    uint8_t* mem_ = nullptr;
    size_t   memLen_ = 0;
    uint32_t xres_ = 0, yres_ = 0;
    uint32_t bpp_  = 0;          // bits per pixel (16 or 32 supported)
    uint32_t lineLength_ = 0;    // bytes per row (stride)
    bool     warnedFmt_ = false;
};

} // namespace sec
