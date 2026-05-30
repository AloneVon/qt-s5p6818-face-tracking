// main.cpp — terminal entry point. Builds the shared state and hardware (board
// drivers or DEV_HOST stubs), wires the four worker threads from the design —
//   T1 VisionThread   : V4L2 capture -> Haar detect -> overlay -> FrameHub
//   T2 DisplayThread   : FrameHub -> status overlay -> Framebuffer
//   T3 BusinessThread  : TrackingState + client cmds -> PID -> servo PWM
//   T4 TcpServer       : accept -> per-client ClientSession threads
// — then runs until SIGINT/SIGTERM and shuts everything down cleanly (stop
// threads, drop clients, release the gimbal and devices).
#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

#include "core/Config.h"
#include "core/FrameHub.h"
#include "core/TrackingState.h"
#include "core/CommandQueue.h"
#include "core/StatusBoard.h"
#include "core/ClientRegistry.h"
#include "core/Log.h"

#include "vision/FaceDetector.h"
#include "servo/PanTiltTracker.h"
#include "file/FileManager.h"

#include "app/Hardware.h"
#include "app/VisionThread.h"
#include "app/DisplayThread.h"
#include "app/BusinessThread.h"
#include "net/TcpServer.h"

namespace {
std::atomic<bool> g_stop{false};
void onSignal(int) { g_stop.store(true); }
}  // namespace

int main() {
    using namespace sec;

    // Graceful stop on Ctrl-C / kill. Ignore SIGPIPE so a write to a closed
    // client returns EPIPE in the session thread instead of killing the process.
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::signal(SIGPIPE, SIG_IGN);

    Config cfg;

    // ---- shared state ----------------------------------------------------
    FrameHub       hub;
    TrackingState  track;
    CommandQueue   cmds;
    StatusBoard    status;
    ClientRegistry registry(status);

    FileManager files(cfg.captureDir, cfg.jpegQuality);
    if (!files.init())
        LOGW("main: capture dir '%s' not ready; file ops may fail",
             cfg.captureDir.c_str());

    // ---- hardware (board drivers or DEV_HOST stubs) ----------------------
    auto camera  = makeCamera(cfg);
    auto display = makeDisplay(cfg);
    auto servo   = makeServo(cfg);

    if (!camera->open()) { LOGE("main: camera open failed; aborting"); return 1; }
    if (!display->open()) LOGW("main: display open failed; running headless");
    if (!servo->init())   LOGW("main: servo init failed; gimbal will not move");

    // ---- face detection --------------------------------------------------
    FaceDetector detector(cfg.cascadePath, cfg.detectScale, cfg.haarScaleFactor,
                          cfg.haarMinNeighbors, cfg.haarMinFacePx);
    if (!detector.load()) {
        LOGE("main: failed to load cascade '%s'; aborting", cfg.cascadePath.c_str());
        camera->close();
        return 1;
    }

    // ---- tracker (P-control parameters straight from Config) -------------
    TrackerParams tp{
        cfg.panMinAngle,  cfg.panMaxAngle,  cfg.panHome,
        cfg.tiltMinAngle, cfg.tiltMaxAngle, cfg.tiltHome,
        cfg.trackDeadzonePx, cfg.trackGainPan, cfg.trackGainTilt,
        cfg.trackMaxStepDeg, cfg.panSign, cfg.tiltSign,
    };
    PanTiltTracker tracker(tp);

    // ---- worker threads --------------------------------------------------
    VisionThread   vision(*camera, detector, hub, track, status, files,
                          cfg.jpegQuality, cfg.autoSnapshotOnFace,
                          cfg.autoSnapshotMinIntervalMs);
    DisplayThread  displayThread(*display, hub, status, cfg.captureFps);
    BusinessThread business(*servo, tracker, track, cmds, status,
                            cfg.servoUpdateMs, cfg.manualOverrideMs);
    TcpServer      server(cfg.tcpPort, cfg.maxClients, hub, cmds, files, registry,
                          cfg.streamFps);

    if (!server.bindAndListen()) {
        LOGE("main: TCP bind failed on port %d; aborting", cfg.tcpPort);
        camera->close();
        return 1;
    }

    vision.start();
    displayThread.start();
    business.start();
    server.start();
    LOGI("main: terminal running on port %d (%dx%d). Ctrl-C to stop.",
         cfg.tcpPort, cfg.captureWidth, cfg.captureHeight);

    while (!g_stop.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    LOGI("main: shutting down...");

    // Stop the network first (no new frames pushed), then the producer/consumer
    // threads, then drop any still-connected clients, then release hardware.
    server.stop();        server.join();
    vision.stop();        vision.join();
    business.stop();      business.join();
    displayThread.stop(); displayThread.join();
    registry.stopAll();

    servo->release();
    camera->close();
    display->close();

    LOGI("main: stopped cleanly");
    return 0;
}
