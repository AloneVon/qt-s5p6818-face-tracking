// FaceDetector.h — Haar-cascade face detection. Runs on a downscaled grayscale
// image for speed on the Cortex-A53, then maps the chosen face back to full-res
// coordinates. Returns the single largest face (the tracking target).
#pragma once

#include <string>
#include <opencv2/objdetect.hpp>

#include "../core/Types.h"

namespace sec {

class FaceDetector {
public:
    FaceDetector(std::string cascadePath, double detectScale,
                 double scaleFactor, int minNeighbors, int minFacePx);

    bool load();

    // Detect the largest face in a BGR frame. Returns true and fills outBox
    // (full-resolution coordinates) when a face is found.
    bool detect(const cv::Mat& bgr, Recti& outBox);

private:
    std::string cascadePath_;
    double detectScale_;
    double scaleFactor_;
    int    minNeighbors_;
    int    minFacePx_;
    cv::CascadeClassifier cascade_;
    cv::Mat gray_, small_;  // reused scratch buffers
};

} // namespace sec
