// DisplayThread.h — T2. Pulls the latest annotated frame from the FrameHub,
// overlays a system status bar (FPS, mode, servo angles, face, client count),
// and pushes it to the local display (framebuffer on the board).
#pragma once

#include "../core/StoppableThread.h"
#include "../core/FrameHub.h"
#include "../core/StatusBoard.h"
#include "../display/IDisplay.h"

namespace sec {

class DisplayThread : public StoppableThread {
public:
    DisplayThread(IDisplay& disp, FrameHub& hub, StatusBoard& status, int targetFps);
    ~DisplayThread() override { stop(); join(); }

protected:
    void run() override;

private:
    IDisplay&    disp_;
    FrameHub&    hub_;
    StatusBoard& status_;
    int          targetFps_;
};

} // namespace sec
