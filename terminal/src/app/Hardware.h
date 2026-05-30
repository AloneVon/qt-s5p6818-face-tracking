// Hardware.h — compile-time hardware factory. Picks the board drivers
// (V4L2Camera / Framebuffer / ServoController) or the desktop dev stubs
// (HostCamera / WindowDisplay / StubServo) via -DDEV_HOST, so main() and the
// worker threads are byte-for-byte identical in both builds. This is the only
// place the #ifdef lives; everything else talks to the ICamera/IDisplay/IServo
// interfaces.
#pragma once

#include <memory>

#include "../core/Config.h"
#include "../capture/ICamera.h"
#include "../display/IDisplay.h"
#include "../servo/IServo.h"

#ifdef DEV_HOST
#include "../capture/HostCamera.h"
#include "../display/WindowDisplay.h"
#include "../servo/StubServo.h"
#else
#include "../capture/V4L2Camera.h"
#include "../display/Framebuffer.h"
#include "../servo/ServoController.h"
#endif

namespace sec {

inline std::unique_ptr<ICamera> makeCamera(const Config& cfg) {
#ifdef DEV_HOST
    return std::make_unique<HostCamera>(0, cfg.captureWidth, cfg.captureHeight,
                                        cfg.captureFps);
#else
    return std::make_unique<V4L2Camera>(cfg.cameraDevice, cfg.captureWidth,
                                        cfg.captureHeight, cfg.captureFps);
#endif
}

inline std::unique_ptr<IDisplay> makeDisplay(const Config& cfg) {
#ifdef DEV_HOST
    (void)cfg;
    return std::make_unique<WindowDisplay>();
#else
    return std::make_unique<Framebuffer>(cfg.fbDevice);
#endif
}

inline std::unique_ptr<IServo> makeServo(const Config& cfg) {
#ifdef DEV_HOST
    (void)cfg;
    return std::make_unique<StubServo>();
#else
    return std::make_unique<ServoController>(cfg.pwmChipPath, cfg.panChannel,
                                             cfg.tiltChannel, cfg.pwmPeriodNs,
                                             cfg.servoMinDutyNs, cfg.servoMaxDutyNs);
#endif
}

} // namespace sec
