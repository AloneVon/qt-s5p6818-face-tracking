#include "DisplayThread.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <opencv2/imgproc.hpp>

#include "../core/Log.h"

namespace sec {

DisplayThread::DisplayThread(IDisplay& disp, FrameHub& hub, StatusBoard& status,
                             int targetFps)
    : disp_(disp), hub_(hub), status_(status),
      targetFps_(targetFps > 0 ? targetFps : 30) {}

void DisplayThread::run() {
    const auto period = std::chrono::milliseconds(1000 / targetFps_);

    while (running()) {
        auto frame = hub_.latest();
        if (!frame || frame->mat.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        cv::Mat canvas = frame->mat.clone();  // clone: hub frame is immutable/shared

        char line[192];
        std::snprintf(line, sizeof(line),
                      "FPS:%.1f  %s  PAN:%.0f TILT:%.0f  FACE:%s  CLIENTS:%d",
                      status_.captureFps.load(),
                      status_.autoTrack.load() ? "AUTO" : "MANUAL",
                      status_.panAngle.load(), status_.tiltAngle.load(),
                      status_.faceFound.load() ? "Y" : "N",
                      status_.clientCount.load());

        cv::rectangle(canvas, cv::Rect(0, 0, canvas.cols, 22),
                      cv::Scalar(0, 0, 0), cv::FILLED);
        cv::putText(canvas, line, cv::Point(6, 16), cv::FONT_HERSHEY_SIMPLEX,
                    0.5, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);

        disp_.show(canvas);
        std::this_thread::sleep_for(period);
    }
    LOGI("DisplayThread: stopped");
}

} // namespace sec
