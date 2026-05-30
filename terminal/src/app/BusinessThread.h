// BusinessThread.h — T3. The control loop: drains client commands (manual
// nudge / absolute / mode), runs auto-tracking when enabled, and writes the
// resulting pan/tilt angles to the servo. A manual command temporarily
// overrides auto-tracking for Config::manualOverrideMs.
#pragma once

#include "../core/StoppableThread.h"
#include "../core/TrackingState.h"
#include "../core/CommandQueue.h"
#include "../core/StatusBoard.h"
#include "../servo/IServo.h"
#include "../servo/PanTiltTracker.h"

namespace sec {

class BusinessThread : public StoppableThread {
public:
    BusinessThread(IServo& servo, PanTiltTracker& tracker, TrackingState& track,
                   CommandQueue& cmds, StatusBoard& status,
                   int updateMs, int manualOverrideMs);
    ~BusinessThread() override { stop(); join(); }

protected:
    void run() override;

private:
    IServo&         servo_;
    PanTiltTracker& tracker_;
    TrackingState&  track_;
    CommandQueue&   cmds_;
    StatusBoard&    status_;
    int             updateMs_;
    int             manualOverrideMs_;
};

} // namespace sec
