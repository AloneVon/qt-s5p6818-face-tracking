// PanTiltTracker.h — converts face-center error into pan/tilt angle targets.
// Proportional control with a deadzone (anti-jitter) and a per-update step
// clamp (smoothness). Holds position when no face is present. Also accepts
// manual nudges / absolute sets from remote clients.
#pragma once

#include "../core/Types.h"
#include "../core/TrackingState.h"

namespace sec {

struct TrackerParams {
    double panMin, panMax, panHome;
    double tiltMin, tiltMax, tiltHome;
    int    deadzonePx;
    double gainPan, gainTilt;
    double maxStepDeg;
    double panSign, tiltSign;
};

class PanTiltTracker {
public:
    explicit PanTiltTracker(const TrackerParams& p)
        : p_(p), pan_(p.panHome), tilt_(p.tiltHome) {}

    void updateAuto(const TrackingState::Snapshot& s);  // P-control toward face
    void nudge(double dPanDeg, double dTiltDeg);        // manual relative
    void setAbs(double panDeg, double tiltDeg);         // manual absolute
    void home();

    double pan() const  { return pan_; }
    double tilt() const { return tilt_; }

private:
    TrackerParams p_;
    double pan_;
    double tilt_;
};

} // namespace sec
