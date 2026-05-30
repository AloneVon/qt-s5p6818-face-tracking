#include "HostCamera.h"
#include "../core/Log.h"

namespace sec {

HostCamera::HostCamera(int index, int width, int height, int fps)
    : index_(index), width_(width), height_(height), fps_(fps) {}

bool HostCamera::open() {
    if (!cap_.open(index_)) {
        LOGE("HostCamera: cannot open capture device index %d", index_);
        return false;
    }
    cap_.set(cv::CAP_PROP_FRAME_WIDTH,  width_);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height_);
    cap_.set(cv::CAP_PROP_FPS,          fps_);
    width_  = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    height_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    LOGI("HostCamera: opened index %d at %dx%d", index_, width_, height_);
    return true;
}

bool HostCamera::grab(cv::Mat& outBgr) {
    return cap_.read(outBgr) && !outBgr.empty();
}

void HostCamera::close() {
    if (cap_.isOpened()) cap_.release();
}

} // namespace sec
