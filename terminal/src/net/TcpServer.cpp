#include "TcpServer.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <thread>

#include "ClientSession.h"
#include "IByteStream.h"
#include "TcpConn.h"
#include "WebSocketConn.h"
#include "../core/ClientRegistry.h"
#include "../core/Log.h"
#ifdef SEC_ENABLE_TLS
#include "TlsConn.h"
#endif

namespace sec {

namespace {
const char* transportName(Transport t) {
    return t == Transport::WebSocket ? "ws" : "tcp";
}
}  // namespace

TcpServer::TcpServer(int port, int maxClients, FrameHub& hub, CommandQueue& cmds,
                     FileManager& files, ClientRegistry& registry, int streamFps,
                     std::string authToken, Transport transport, TlsContext* tls)
    : port_(port), maxClients_(maxClients > 0 ? maxClients : 1),
      hub_(hub), cmds_(cmds), files_(files), registry_(registry),
      streamFps_(streamFps), authToken_(std::move(authToken)),
      transport_(transport), tls_(tls) {}

bool TcpServer::bindAndListen() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        LOGE("TcpServer: socket() failed: %s", std::strerror(errno));
        return false;
    }

    int one = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOGE("TcpServer: bind(port=%d) failed: %s", port_, std::strerror(errno));
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (::listen(listenFd_, 8) < 0) {
        LOGE("TcpServer: listen() failed: %s", std::strerror(errno));
        ::close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    LOGI("TcpServer[%s%s]: listening on port %d (max %d clients)",
         transportName(transport_), tls_ ? "+tls" : "", port_, maxClients_);
    return true;
}

void TcpServer::run() {
    if (listenFd_ < 0) {
        LOGE("TcpServer: run() called without a listening socket");
        return;
    }
    while (running()) {
        pollfd pfd{ listenFd_, POLLIN, 0 };
        int pr = ::poll(&pfd, 1, 200);  // 200 ms wake-up: also our reap cadence
        if (pr < 0) {
            if (errno == EINTR) continue;
            LOGE("TcpServer: poll() failed: %s", std::strerror(errno));
            break;
        }
        if (pr > 0 && (pfd.revents & POLLIN)) acceptOne();
        registry_.reapFinished();  // join + drop any disconnected sessions
    }
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    LOGI("TcpServer: accept loop stopped");
}

void TcpServer::acceptOne() {
    sockaddr_in cli{};
    socklen_t len = sizeof(cli);
    int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&cli), &len);
    if (fd < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            LOGW("TcpServer: accept() failed: %s", std::strerror(errno));
        return;
    }

    if (registry_.count() >= maxClients_) {
        LOGW("TcpServer: client cap (%d) reached, refusing %s",
             maxClients_, "incoming connection");
        ::close(fd);
        return;
    }

    char ip[INET_ADDRSTRLEN] = {0};
    ::inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
    std::string peer = std::string(ip) + ":" + std::to_string(ntohs(cli.sin_port));
    LOGI("TcpServer[%s%s]: client connected %s (fd=%d)",
         transportName(transport_), tls_ ? "+tls" : "", peer.c_str(), fd);

    // Below the SEC1/WS framing: a plaintext socket, or a TLS session when this
    // listener was given a context. The two wrappers above don't know which.
    std::unique_ptr<IByteStream> stream;
#ifdef SEC_ENABLE_TLS
    if (tls_)
        stream = std::make_unique<TlsByteStream>(tls_->ctx(), fd);
    else
#endif
        stream = std::make_unique<RawByteStream>(fd);

    std::unique_ptr<IConn> conn;
    if (transport_ == Transport::WebSocket)
        conn = std::make_unique<WebSocketConn>(std::move(stream));
    else
        conn = std::make_unique<TcpConn>(std::move(stream));

    auto session = std::make_shared<ClientSession>(std::move(conn), peer, hub_,
                                                   cmds_, files_, streamFps_,
                                                   authToken_);
    std::thread th([session] { session->run(); });
    registry_.adopt(session, std::move(th));
}

} // namespace sec
