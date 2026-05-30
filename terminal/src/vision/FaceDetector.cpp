#include "FaceDetector.h"

#include <vector>
#include <opencv2/imgproc.hpp>

#include "../core/Log.h"

namespace sec {

FaceDetector::FaceDetector(std::string cascadePath, double detectScale,
                           double scaleFactor, int minNeighbors, int minFacePx)
    : cascadePath_(std::move(cascadePath)), detectScale_(detectScale),
      scaleFactor_(scaleFactor), minNeighbors_(minNeighbors),
      minFacePx_(minFacePx) {}

bool FaceDetector::load() {
    if (!cascade_.load(cascadePath_)) {
        LOGE("FaceDetector: failed to load cascade '%s'", cascadePath_.c_str());
        return false;
    }
    LOGI("FaceDetector: loaded cascade '%s'", cascadePath_.c_str());
    return true;
}

bool FaceDetector::detect(const cv::Mat& bgr, Recti& outBox) {
    if (bgr.empty()) return false;

    cv::cvtColor(bgr, gray_, cv::COLOR_BGR2GRAY);

    const double s = (detectScale_ > 0.0 && detectScale_ < 1.0) ? detectScale_ : 1.0;
    if (s != 1.0)
        cv::resize(gray_, small_, cv::Size(), s, s, cv::INTER_LINEAR);
    else
        small_ = gray_;
    cv::equalizeHist(small_, small_);

    const int minSizeScaled = std::max(1, static_cast<int>(minFacePx_ * s));
    std::vector<cv::Rect> faces;
    cascade_.detectMultiScale(small_, faces, scaleFactor_, minNeighbors_,
                              0, cv::Size(minSizeScaled, minSizeScaled));
    if (faces.empty()) return false;

    // Pick the largest face (closest / most prominent subject).
    const cv::Rect* best = &faces[0];
    for (const auto& f : faces)
        if (f.area() > best->area()) best = &f;

    const double inv = 1.0 / s;  // map back to full resolution
    outBox.x = static_cast<int>(best->x * inv);
    outBox.y = static_cast<int>(best->y * inv);
    outBox.w = static_cast<int>(best->width  * inv);
    outBox.h = static_cast<int>(best->height * inv);
    return true;
}

} // namespace sec
