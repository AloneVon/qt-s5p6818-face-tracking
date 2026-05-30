// StubServo.h — DEV_HOST servo: no hardware, just throttled logging of the
// target angles so the tracking control loop can be validated on a desktop.
#pragma once

#include "IServo.h"
#include "../core/Log.h"
#include "../core/Time.h"

namespace sec {

class StubServo : public IServo {
public:
    bool init() override { LOGI("StubServo: init (no PWM hardware)"); return true; }

    void setAngles(double panDeg, double tiltDeg) override {
        const uint64_t now = nowMs();
        if (now - lastLog_ >= 500) {  // ~2 Hz
            LOGI("StubServo: pan=%.1f tilt=%.1f", panDeg, tiltDeg);
            lastLog_ = now;
        }
    }

    void release() override {}

private:
    uint64_t lastLog_ = 0;
};

} // namespace sec
