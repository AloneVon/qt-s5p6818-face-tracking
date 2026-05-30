#include "WindowDisplay.h"

#ifdef DEV_HOST
#include <opencv2/highgui.hpp>
#endif

namespace sec {

WindowDisplay::WindowDisplay(std::string title) : title_(std::move(title)) {}

#ifdef DEV_HOST
bool WindowDisplay::open() {
    cv::namedWindow(title_, cv::WINDOW_AUTOSIZE);
    return true;
}
void WindowDisplay::show(const cv::Mat& bgr) {
    if (!bgr.empty()) { cv::imshow(title_, bgr); cv::waitKey(1); }
}
void WindowDisplay::close() { cv::destroyAllWindows(); }
#else
bool WindowDisplay::open() { return false; }
void WindowDisplay::show(const cv::Mat&) {}
void WindowDisplay::close() {}
#endif

} // namespace sec
