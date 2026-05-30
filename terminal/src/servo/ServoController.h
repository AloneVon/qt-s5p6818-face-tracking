// ServoController.h — pan/tilt servos driven through the kernel PWM sysfs API:
//   /sys/class/pwm/pwmchipN/{export, pwm<ch>/{period,duty_cycle,enable}}
// Standard hobby servo: 20 ms period (50 Hz), 1.0-2.0 ms pulse maps 0-180 deg.
#pragma once

#include <string>
#include "IServo.h"

namespace sec {

class ServoController : public IServo {
public:
    ServoController(std::string chipPath, int panChannel, int tiltChannel,
                    long periodNs, long minDutyNs, long maxDutyNs);
    ~ServoController() override;

    bool init() override;
    void setAngles(double panDeg, double tiltDeg) override;
    void release() override;

private:
    bool exportChannel(int ch);
    bool configChannel(int ch);
    bool writeStr(const std::string& path, const std::string& value);
    std::string chanDir(int ch) const;     // .../pwm<ch>
    long angleToDutyNs(double angleDeg) const;
    void setChannelAngle(int ch, double angleDeg);

    std::string chipPath_;
    int  panCh_, tiltCh_;
    long periodNs_, minDutyNs_, maxDutyNs_;
    bool ready_ = false;
};

} // namespace sec
