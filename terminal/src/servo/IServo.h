// IServo.h — gimbal abstraction. Board uses ServoController (PWM sysfs);
// host-dev uses StubServo (logs target angles). Angles are degrees.
#pragma once

namespace sec {

class IServo {
public:
    virtual ~IServo() = default;
    virtual bool init() = 0;
    virtual void setAngles(double panDeg, double tiltDeg) = 0;
    virtual void release() = 0;
};

} // namespace sec
