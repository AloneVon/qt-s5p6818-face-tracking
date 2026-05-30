#include "V4L2Camera.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/videodev2.h>

#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../core/Log.h"

namespace sec {

// ioctl wrapper that retries while interrupted by a signal.
static int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ::ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Camera::V4L2Camera(std::string device, int width, int height, int fps)
    : device_(std::move(device)), width_(width), height_(height), fps_(fps) {}

V4L2Camera::~V4L2Camera() { close(); }

bool V4L2Camera::open() {
    fd_ = ::open(device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        LOGE("V4L2: open %s failed: %s", device_.c_str(), std::strerror(errno));
        return false;
    }

    v4l2_capability cap{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        LOGE("V4L2: VIDIOC_QUERYCAP failed: %s", std::strerror(errno));
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGE("V4L2: %s is not a video capture device", device_.c_str());
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOGE("V4L2: %s does not support streaming I/O", device_.c_str());
        return false;
    }

    return initFormat() && initBuffers() && startStreaming();
}

bool V4L2Camera::initFormat() {
    // Try MJPEG first (compact, common on UVC webcams), then fall back to YUYV.
    const uint32_t candidates[] = { V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV };
    for (uint32_t fmt : candidates) {
        v4l2_format f{};
        f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        f.fmt.pix.width       = static_cast<__u32>(width_);
        f.fmt.pix.height      = static_cast<__u32>(height_);
        f.fmt.pix.pixelformat = fmt;
        f.fmt.pix.field       = V4L2_FIELD_ANY;
        if (xioctl(fd_, VIDIOC_S_FMT, &f) < 0) continue;
        if (f.fmt.pix.pixelformat != fmt) continue;  // driver refused this fmt

        pixelFormat_ = f.fmt.pix.pixelformat;
        width_  = static_cast<int>(f.fmt.pix.width);   // driver may adjust
        height_ = static_cast<int>(f.fmt.pix.height);

        // Best-effort frame rate request (ignored by some drivers).
        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = static_cast<__u32>(fps_);
        xioctl(fd_, VIDIOC_S_PARM, &parm);

        LOGI("V4L2: %s using %s %dx%d",
             device_.c_str(),
             fmt == V4L2_PIX_FMT_MJPEG ? "MJPEG" : "YUYV",
             width_, height_);
        return true;
    }
    LOGE("V4L2: neither MJPEG nor YUYV accepted at %dx%d", width_, height_);
    return false;
}

bool V4L2Camera::initBuffers() {
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOGE("V4L2: VIDIOC_REQBUFS failed: %s", std::strerror(errno));
        return false;
    }
    if (req.count < 2) {
        LOGE("V4L2: insufficient buffer memory");
        return false;
    }

    buffers_.resize(req.count);
    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            LOGE("V4L2: VIDIOC_QUERYBUF[%u] failed: %s", i, std::strerror(errno));
            return false;
        }
        void* p = ::mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd_, buf.m.offset);
        if (p == MAP_FAILED) {
            LOGE("V4L2: mmap[%u] failed: %s", i, std::strerror(errno));
            return false;
        }
        buffers_[i].start  = p;
        buffers_[i].length = buf.length;
    }
    return true;
}

bool V4L2Camera::startStreaming() {
    for (unsigned i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            LOGE("V4L2: initial VIDIOC_QBUF[%u] failed: %s", i, std::strerror(errno));
            return false;
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        LOGE("V4L2: VIDIOC_STREAMON failed: %s", std::strerror(errno));
        return false;
    }
    streaming_ = true;
    return true;
}

bool V4L2Camera::grab(cv::Mat& outBgr) {
    if (fd_ < 0) return false;

    // Wait until a frame is ready (1s timeout guards against a dead device).
    pollfd pfd{ fd_, POLLIN, 0 };
    int pr = ::poll(&pfd, 1, 1000);
    if (pr <= 0) {
        if (pr < 0 && errno != EINTR)
            LOGW("V4L2: poll error: %s", std::strerror(errno));
        return false;
    }

    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return false;
        LOGW("V4L2: VIDIOC_DQBUF failed: %s", std::strerror(errno));
        return false;
    }

    bool ok = false;
    const uint8_t* data = static_cast<const uint8_t*>(buffers_[buf.index].start);
    if (pixelFormat_ == V4L2_PIX_FMT_MJPEG) {
        cv::Mat enc(1, static_cast<int>(buf.bytesused), CV_8UC1,
                    const_cast<uint8_t*>(data));
        outBgr = cv::imdecode(enc, cv::IMREAD_COLOR);
        ok = !outBgr.empty();
    } else { // YUYV
        cv::Mat yuyv(height_, width_, CV_8UC2, const_cast<uint8_t*>(data));
        cv::cvtColor(yuyv, outBgr, cv::COLOR_YUV2BGR_YUYV);
        ok = !outBgr.empty();
    }

    // Re-queue the buffer for reuse (must happen after we copied/decoded it).
    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0)
        LOGW("V4L2: VIDIOC_QBUF (requeue) failed: %s", std::strerror(errno));

    return ok;
}

void V4L2Camera::close() {
    if (streaming_) {
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
    for (auto& b : buffers_)
        if (b.start && b.start != MAP_FAILED) ::munmap(b.start, b.length);
    buffers_.clear();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

} // namespace sec
