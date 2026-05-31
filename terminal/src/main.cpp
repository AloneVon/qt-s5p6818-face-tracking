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
#include <string>
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
#ifdef SEC_ENABLE_TLS
#include "net/TlsConn.h"
#endif

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
                          cfg.streamFps, cfg.authToken);

    if (!server.bindAndListen()) {
        LOGE("main: TCP bind failed on port %d; aborting", cfg.tcpPort);
        camera->close();
        return 1;
    }

    // Second listener: SEC1 over WebSocket for the mobile client. Shares the
    // registry (so maxClients caps both transports). A failed WS bind is
    // non-fatal — the TCP path still serves the PC client.
    std::unique_ptr<TcpServer> wsServer;
    if (cfg.wsEnabled) {
        wsServer = std::make_unique<TcpServer>(cfg.wsPort, cfg.maxClients, hub, cmds,
                                               files, registry, cfg.streamFps,
                                               cfg.authToken, Transport::WebSocket);
        if (!wsServer->bindAndListen()) {
            LOGW("main: WebSocket bind failed on port %d; mobile client disabled",
                 cfg.wsPort);
            wsServer.reset();
        }
    }

    // Optional encrypted twins of the two ports above (TLS for the PC client, wss
    // for the mobile client). Opened only when a cert+key are configured and this
    // binary was built with TLS support. They share the registry and token gate,
    // so maxClients and auth span every transport. The plaintext ports stay open
    // (TLS is opt-in/additive), so the mocks and smoke-test are unaffected.
    // `tls` is declared before the servers so the SSL_CTX outlives them.
#ifdef SEC_ENABLE_TLS
    std::unique_ptr<TlsContext> tls;
#endif
    std::unique_ptr<TcpServer> tlsServer, wssServer;
#ifdef SEC_ENABLE_TLS
    if (!cfg.tlsCertFile.empty() && !cfg.tlsKeyFile.empty()) {
        tls = TlsContext::create(cfg.tlsCertFile, cfg.tlsKeyFile);
        if (!tls) {
            LOGE("main: TLS cert/key set but context failed to load; TLS disabled");
        } else {
            tlsServer = std::make_unique<TcpServer>(cfg.tlsTcpPort, cfg.maxClients, hub,
                                                    cmds, files, registry, cfg.streamFps,
                                                    cfg.authToken, Transport::Tcp, tls.get());
            if (!tlsServer->bindAndListen()) {
                LOGW("main: TLS bind failed on port %d; secure PC client disabled",
                     cfg.tlsTcpPort);
                tlsServer.reset();
            }
            if (cfg.wsEnabled) {
                wssServer = std::make_unique<TcpServer>(cfg.tlsWsPort, cfg.maxClients, hub,
                                                        cmds, files, registry, cfg.streamFps,
                                                        cfg.authToken, Transport::WebSocket,
                                                        tls.get());
                if (!wssServer->bindAndListen()) {
                    LOGW("main: wss bind failed on port %d; secure mobile client disabled",
                         cfg.tlsWsPort);
                    wssServer.reset();
                }
            }
        }
    }
#else
    if (!cfg.tlsCertFile.empty())
        LOGW("main: TLS cert configured but binary built without TLS support "
             "(rebuild with TLS=1 / CONFIG+=tls); ignoring");
#endif

    vision.start();
    displayThread.start();
    business.start();
    server.start();
    if (wsServer)  wsServer->start();
    if (tlsServer) tlsServer->start();
    if (wssServer) wssServer->start();
    std::string extra = wsServer ? " + WS " + std::to_string(cfg.wsPort) : "";
    if (tlsServer) extra += " + TLS " + std::to_string(cfg.tlsTcpPort);
    if (wssServer) extra += " + WSS " + std::to_string(cfg.tlsWsPort);
    LOGI("main: terminal running on TCP %d%s (%dx%d). Ctrl-C to stop.",
         cfg.tcpPort, extra.c_str(), cfg.captureWidth, cfg.captureHeight);

    while (!g_stop.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    LOGI("main: shutting down...");

    // Stop the network first (no new frames pushed), then the producer/consumer
    // threads, then drop any still-connected clients, then release hardware.
    server.stop();        server.join();
    if (wsServer)  { wsServer->stop();  wsServer->join(); }
    if (tlsServer) { tlsServer->stop(); tlsServer->join(); }
    if (wssServer) { wssServer->stop(); wssServer->join(); }
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
