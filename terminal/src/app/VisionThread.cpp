#include "VisionThread.h"

#include <chrono>
#include <thread>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "../core/Log.h"
#include "../core/Time.h"
#include "../file/FileManager.h"

namespace sec {

VisionThread::VisionThread(ICamera& cam, FaceDetector& det, FrameHub& hub,
                           TrackingState& track, StatusBoard& status,
                           FileManager& files, int jpegQuality,
                           bool autoSnapshot, int autoSnapshotIntervalMs)
    : cam_(cam), det_(det), hub_(hub), track_(track), status_(status),
      files_(files), jpegQuality_(jpegQuality), autoSnapshot_(autoSnapshot),
      autoSnapshotIntervalMs_(autoSnapshotIntervalMs) {}

void VisionThread::run() {
    const cv::Scalar kGreen(0, 255, 0), kYellow(0, 255, 255), kCyan(255, 255, 0);
    cv::Mat frame;
    Recti box;
    uint32_t seq = 0;
    double fps = 0.0;
    uint64_t lastT = nowMs();
    uint64_t lastSnapMs = 0;
    bool prevFound = false;
    int fails = 0;

    while (running()) {
        if (!cam_.grab(frame) || frame.empty()) {
            if (++fails % 30 == 1) LOGW("VisionThread: camera grab failed");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        fails = 0;

        const bool found = det_.detect(frame, box);

        // Overlay: frame-center crosshair, face box, and an error line from the
        // frame center to the face center (visualizes what the servo corrects).
        const int cx = frame.cols / 2, cy = frame.rows / 2;
        cv::drawMarker(frame, {cx, cy}, kCyan, cv::MARKER_CROSS, 18, 1);
        if (found) {
            cv::rectangle(frame, cv::Rect(box.x, box.y, box.w, box.h), kGreen, 2);
            const Point2i c = box.center();
            cv::circle(frame, {c.x, c.y}, 3, kGreen, cv::FILLED);
            cv::line(frame, {cx, cy}, {c.x, c.y}, kYellow, 1);
        }

        // Build one immutable frame: clone the annotated Mat for the display,
        // and encode the JPEG once here for all client sessions.
        auto f = std::make_shared<Frame>();
        f->mat    = frame.clone();
        f->seq    = seq++;
        f->width  = frame.cols;
        f->height = frame.rows;
        f->tsMs   = nowMs();
        cv::imencode(".jpg", frame, f->jpeg,
                     { cv::IMWRITE_JPEG_QUALITY, jpegQuality_ });
        hub_.publish(f);

        track_.update(found, found ? box : Recti{}, frame.cols, frame.rows);
        status_.faceFound.store(found);

        // FPS as an exponential moving average.
        const uint64_t now = nowMs();
        const double dt = (now - lastT) / 1000.0;
        lastT = now;
        if (dt > 0.0) fps = (fps == 0.0) ? (1.0 / dt) : (fps * 0.9 + (1.0 / dt) * 0.1);
        status_.captureFps.store(fps);

        // Debounced auto-snapshot on the rising edge of a detection.
        if (autoSnapshot_ && found && !prevFound &&
            now - lastSnapMs >= static_cast<uint64_t>(autoSnapshotIntervalMs_)) {
            files_.snapshot(frame);
            lastSnapMs = now;
        }
        prevFound = found;
    }
    LOGI("VisionThread: stopped (last seq=%u)", seq);
}

} // namespace sec
