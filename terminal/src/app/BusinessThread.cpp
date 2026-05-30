#include "BusinessThread.h"

#include <chrono>
#include <thread>

#include "../core/Log.h"
#include "../core/Time.h"

namespace sec {

BusinessThread::BusinessThread(IServo& servo, PanTiltTracker& tracker,
                               TrackingState& track, CommandQueue& cmds,
                               StatusBoard& status, int updateMs,
                               int manualOverrideMs)
    : servo_(servo), tracker_(tracker), track_(track), cmds_(cmds),
      status_(status), updateMs_(updateMs > 0 ? updateMs : 20),
      manualOverrideMs_(manualOverrideMs) {}

void BusinessThread::run() {
    const auto period = std::chrono::milliseconds(updateMs_);
    bool     autoEnabled = true;
    uint64_t manualUntil = 0;

    while (running()) {
        // Apply any pending client commands.
        Command c;
        while (cmds_.tryPop(c)) {
            switch (c.type) {
                case CommandType::ServoStep:
                    tracker_.nudge(c.pan, c.tilt);
                    manualUntil = nowMs() + manualOverrideMs_;
                    break;
                case CommandType::ServoAbs:
                    tracker_.setAbs(c.pan, c.tilt);
                    manualUntil = nowMs() + manualOverrideMs_;
                    break;
                case CommandType::SetMode:
                    autoEnabled = c.autoTrack;
                    if (autoEnabled) manualUntil = 0;  // resume immediately
                    break;
            }
        }

        const bool effectiveAuto = autoEnabled && nowMs() >= manualUntil;
        if (effectiveAuto) tracker_.updateAuto(track_.get());

        servo_.setAngles(tracker_.pan(), tracker_.tilt());

        status_.autoTrack.store(effectiveAuto);
        status_.panAngle.store(tracker_.pan());
        status_.tiltAngle.store(tracker_.tilt());

        std::this_thread::sleep_for(period);
    }
    LOGI("BusinessThread: stopped");
}

} // namespace sec
