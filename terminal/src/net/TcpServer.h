// TcpServer.h — T4. Owns the listening socket and the accept loop. For each
// accepted connection it spawns a ClientSession on its own thread, handed to the
// ClientRegistry for lifetime management (matching the spec's "每个新客户端开启
// 新线程负责收发"). A single poll()-driven loop lets stop() interrupt the wait
// promptly and gives us a natural cadence to reap finished sessions.
#pragma once

#include "../core/StoppableThread.h"

namespace sec {

class FrameHub;
class CommandQueue;
class FileManager;
class ClientRegistry;

// Which byte transport accepted connections speak. Tcp hands the session a raw
// socket (the PC client); WebSocket performs an RFC 6455 upgrade first (the
// browser/Capacitor mobile client). Everything above IConn is identical.
enum class Transport { Tcp, WebSocket };

class TcpServer : public StoppableThread {
public:
    TcpServer(int port, int maxClients, FrameHub& hub, CommandQueue& cmds,
              FileManager& files, ClientRegistry& registry, int streamFps,
              Transport transport = Transport::Tcp);
    ~TcpServer() override { stop(); join(); }

    // Create/bind/listen the socket. Call once before start(); returns false on
    // failure (e.g. port in use) so main() can report and exit cleanly.
    bool bindAndListen();

protected:
    void run() override;  // accept loop, on the server's own thread

private:
    void acceptOne();

    int             port_;
    int             maxClients_;
    FrameHub&       hub_;
    CommandQueue&   cmds_;
    FileManager&    files_;
    ClientRegistry& registry_;
    int             streamFps_;
    Transport       transport_;
    int             listenFd_ = -1;
};

} // namespace sec
