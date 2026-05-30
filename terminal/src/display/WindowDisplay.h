// WindowDisplay.h — DEV_HOST display via cv::imshow. Only instantiated under
// DEV_HOST (the .cpp body is compiled out otherwise, since the board's minimal
// OpenCV build omits highgui).
#pragma once

#include <string>
#include "IDisplay.h"

namespace sec {

class WindowDisplay : public IDisplay {
public:
    explicit WindowDisplay(std::string title = "S5P6818 terminal (dev)");
    bool open() override;
    void show(const cv::Mat& bgr) override;
    void close() override;

private:
    std::string title_;
};

} // namespace sec
