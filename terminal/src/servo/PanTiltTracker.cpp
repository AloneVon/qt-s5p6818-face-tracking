#include "PanTiltTracker.h"

#include <algorithm>
#include <cmath>

namespace sec {

void PanTiltTracker::updateAuto(const TrackingState::Snapshot& s) {
    if (!s.found || s.frameW <= 0 || s.frameH <= 0) return;  // hold position

    const double ex = s.center.x - s.frameW / 2.0;  // +ve: face right of center
    const double ey = s.center.y - s.frameH / 2.0;  // +ve: face below center

    double dPan = 0.0, dTilt = 0.0;
    if (std::abs(ex) > p_.deadzonePx) dPan  = p_.panSign  * p_.gainPan  * ex;
    if (std::abs(ey) > p_.deadzonePx) dTilt = p_.tiltSign * p_.gainTilt * ey;

    dPan  = std::clamp(dPan,  -p_.maxStepDeg, p_.maxStepDeg);
    dTilt = std::clamp(dTilt, -p_.maxStepDeg, p_.maxStepDeg);

    pan_  = std::clamp(pan_  + dPan,  p_.panMin,  p_.panMax);
    tilt_ = std::clamp(tilt_ + dTilt, p_.tiltMin, p_.tiltMax);
}

void PanTiltTracker::nudge(double dPanDeg, double dTiltDeg) {
    pan_  = std::clamp(pan_  + dPanDeg,  p_.panMin,  p_.panMax);
    tilt_ = std::clamp(tilt_ + dTiltDeg, p_.tiltMin, p_.tiltMax);
}

void PanTiltTracker::setAbs(double panDeg, double tiltDeg) {
    pan_  = std::clamp(panDeg,  p_.panMin,  p_.panMax);
    tilt_ = std::clamp(tiltDeg, p_.tiltMin, p_.tiltMax);
}

void PanTiltTracker::home() {
    pan_  = p_.panHome;
    tilt_ = p_.tiltHome;
}

} // namespace sec
