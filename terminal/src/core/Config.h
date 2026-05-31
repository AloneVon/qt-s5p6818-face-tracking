// Config.h — central, board-specific tunables for the S5P6818 terminal.
// Everything that differs between boards/cameras/gimbals lives here so the rest
// of the code stays portable. Construct one Config in main() and pass the parts
// each component needs.
#pragma once

#include <string>

namespace sec {

struct Config {
    // ---- Camera (V4L2) ----------------------------------------------------
    std::string cameraDevice = "/dev/video0";
    int         captureWidth  = 640;
    int         captureHeight = 480;
    int         captureFps    = 30;
    // Preferred pixel format: try MJPEG first (smaller, most UVC cams), then
    // fall back to YUYV. V4L2Camera negotiates and records what it actually got.

    // ---- Framebuffer ------------------------------------------------------
    std::string fbDevice = "/dev/fb0";

    // ---- Face detection ---------------------------------------------------
    std::string cascadePath = "models/haarcascade_frontalface_default.xml";
    // Run detection on a downscaled gray image for speed on the A53 CPU.
    double      detectScale     = 0.5;   // 0.5 => detect on half-res
    double      haarScaleFactor = 1.2;
    int         haarMinNeighbors = 4;
    int         haarMinFacePx   = 60;    // min face size at full res

    // ---- Servo / PWM (sysfs) ---------------------------------------------
    // Two channels on a PWM chip: /sys/class/pwm/pwmchip<chip>/pwm<ch>/...
    std::string pwmChipPath = "/sys/class/pwm/pwmchip0";
    int         panChannel  = 0;         // horizontal
    int         tiltChannel = 1;         // vertical
    long        pwmPeriodNs = 20000000;  // 20 ms => 50 Hz servo frame
    long        servoMinDutyNs = 1000000;  // 1.0 ms => angle 0
    long        servoMaxDutyNs = 2000000;  // 2.0 ms => angle 180
    double      panMinAngle  = 10.0, panMaxAngle  = 170.0, panHome  = 90.0;
    double      tiltMinAngle = 40.0, tiltMaxAngle = 140.0, tiltHome = 90.0;

    // ---- Tracking control (P controller w/ deadzone) ----------------------
    int    trackDeadzonePx = 30;     // ignore error smaller than this
    double trackGainPan    = 0.03;   // deg per pixel of horizontal error
    double trackGainTilt   = 0.03;   // deg per pixel of vertical error
    double trackMaxStepDeg = 4.0;    // clamp per-update angle change
    int    manualOverrideMs   = 4000;// after a manual cmd, pause auto-track this long
    int    servoUpdateMs      = 20;  // business/servo loop period (~50 Hz)
    // Direction of correction — FLIP THESE if the gimbal chases the wrong way.
    // Default: face right (errorX>0) -> increase pan; face down (errorY>0,
    // y grows downward) -> decrease tilt.
    double panSign  =  1.0;
    double tiltSign = -1.0;

    // ---- Networking -------------------------------------------------------
    int    tcpPort        = 8888;
    // SEC1 over WebSocket for the browser/Capacitor mobile client, which cannot
    // open a raw TCP socket. Same protocol, one SEC1 frame per binary message.
    bool   wsEnabled      = true;
    int    wsPort         = 8889;
    int    streamFps      = 15;      // frames/sec pushed to each client
    int    jpegQuality    = 70;      // cv::imencode quality 0..100
    int    maxClients     = 8;

    // ---- File manager -----------------------------------------------------
    std::string captureDir = "captures";
    bool   autoSnapshotOnFace = false;   // debounced snapshot when a face appears
    int    autoSnapshotMinIntervalMs = 5000;
};

} // namespace sec
