#include "Framebuffer.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/fb.h>

#include <algorithm>
#include <opencv2/imgproc.hpp>

#include "../core/Log.h"

namespace sec {

Framebuffer::Framebuffer(std::string device) : device_(std::move(device)) {}
Framebuffer::~Framebuffer() { close(); }

bool Framebuffer::open() {
    fd_ = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        LOGE("Framebuffer: open %s failed: %s", device_.c_str(), std::strerror(errno));
        return false;
    }
    fb_var_screeninfo var{};
    fb_fix_screeninfo fix{};
    if (::ioctl(fd_, FBIOGET_VSCREENINFO, &var) < 0 ||
        ::ioctl(fd_, FBIOGET_FSCREENINFO, &fix) < 0) {
        LOGE("Framebuffer: FBIOGET_*SCREENINFO failed: %s", std::strerror(errno));
        return false;
    }
    xres_ = var.xres;
    yres_ = var.yres;
    bpp_  = var.bits_per_pixel;
    lineLength_ = fix.line_length;
    memLen_ = fix.smem_len;

    mem_ = static_cast<uint8_t*>(
        ::mmap(nullptr, memLen_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0));
    if (mem_ == MAP_FAILED) {
        LOGE("Framebuffer: mmap failed: %s", std::strerror(errno));
        mem_ = nullptr;
        return false;
    }
    std::memset(mem_, 0, memLen_);  // clear to black once
    LOGI("Framebuffer: %s %ux%u %ubpp stride=%u", device_.c_str(),
         xres_, yres_, bpp_, lineLength_);
    return true;
}

void Framebuffer::show(const cv::Mat& bgr) {
    if (!mem_ || bgr.empty()) return;

    // Fit the frame within the screen, preserving aspect ratio.
    cv::Mat src = bgr;
    if (static_cast<uint32_t>(src.cols) > xres_ ||
        static_cast<uint32_t>(src.rows) > yres_) {
        double sc = std::min(static_cast<double>(xres_) / src.cols,
                             static_cast<double>(yres_) / src.rows);
        cv::resize(bgr, src, cv::Size(), sc, sc, cv::INTER_AREA);
    }

    // Convert to the device pixel format.
    cv::Mat conv;
    int bytesPerPixel;
    if (bpp_ == 16) {
        cv::cvtColor(src, conv, cv::COLOR_BGR2BGR565);
        bytesPerPixel = 2;
    } else if (bpp_ == 32) {
        cv::cvtColor(src, conv, cv::COLOR_BGR2BGRA);
        bytesPerPixel = 4;
    } else {
        if (!warnedFmt_) {
            LOGW("Framebuffer: unsupported %ubpp (need 16 or 32)", bpp_);
            warnedFmt_ = true;
        }
        return;
    }

    const int offx = (static_cast<int>(xres_) - conv.cols) / 2;
    const int offy = (static_cast<int>(yres_) - conv.rows) / 2;
    const size_t rowBytes = static_cast<size_t>(conv.cols) * bytesPerPixel;

    for (int r = 0; r < conv.rows; ++r) {
        uint8_t* dst = mem_ + static_cast<size_t>(offy + r) * lineLength_
                            + static_cast<size_t>(offx) * bytesPerPixel;
        std::memcpy(dst, conv.ptr(r), rowBytes);
    }
}

void Framebuffer::close() {
    if (mem_ && mem_ != MAP_FAILED) {
        std::memset(mem_, 0, memLen_);  // leave screen black
        ::munmap(mem_, memLen_);
    }
    mem_ = nullptr;
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

} // namespace sec
