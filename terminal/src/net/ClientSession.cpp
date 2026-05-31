#include "ClientSession.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <unistd.h>

#include "MiniJson.h"
#include "SocketUtil.h"
#include "../core/CommandQueue.h"
#include "../core/FrameHub.h"
#include "../core/Log.h"
#include "../core/Time.h"
#include "../file/FileManager.h"

namespace sec {

using proto::MsgType;

ClientSession::ClientSession(std::unique_ptr<IConn> conn, std::string peer,
                             FrameHub& hub, CommandQueue& cmds, FileManager& files,
                             int streamFps)
    : conn_(std::move(conn)), peer_(std::move(peer)), hub_(hub), cmds_(cmds),
      files_(files), streamFps_(streamFps > 0 ? streamFps : 15) {}

void ClientSession::run() {
    netutil::setTimeouts(conn_->fd(), 5);  // also bounds the handshake read below
    if (!conn_->handshake()) {             // WS upgrade (no-op for raw TCP)
        LOGW("ClientSession[%s]: handshake failed", peer_.c_str());
        conn_->close();
        finished_.store(true);
        return;
    }
    sendJson(MsgType::Hello, "{\"role\":\"terminal\",\"ver\":1}");

    const uint64_t frameIntervalMs = 1000 / static_cast<uint64_t>(streamFps_);
    uint64_t nextFrameAt = 0;
    std::vector<uint8_t> rx;

    while (!stop_.load()) {
        const uint64_t now = nowMs();
        int timeout = (nextFrameAt > now) ? static_cast<int>(nextFrameAt - now) : 0;
        if (timeout > 50) timeout = 50;  // re-check stop_ at least every 50 ms

        pollfd pfd{ conn_->fd(), POLLIN, 0 };
        int pr = ::poll(&pfd, 1, timeout);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr > 0) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (pfd.revents & POLLIN) {
                if (!readAndDispatch(rx)) break;  // peer closed or protocol error
            }
        }
        if (nowMs() >= nextFrameAt) {
            if (!sendFrameIfNew()) break;         // socket dead
            nextFrameAt = nowMs() + frameIntervalMs;
        }
    }

    conn_->close();
    finished_.store(true);
    LOGI("ClientSession[%s]: closed", peer_.c_str());
}

bool ClientSession::readAndDispatch(std::vector<uint8_t>& rx) {
    uint8_t tmp[8192];
    int n = conn_->recv(tmp, sizeof(tmp));
    if (n == 0) return false;  // orderly shutdown
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return true;
        return false;
    }
    rx.insert(rx.end(), tmp, tmp + n);

    std::size_t off = 0;
    while (rx.size() - off >= proto::kHeaderSize) {
        proto::Header h;
        if (!proto::decodeHeader(rx.data() + off, rx.size() - off, h)) {
            LOGW("ClientSession[%s]: bad magic, dropping", peer_.c_str());
            return false;
        }
        if (h.length > 32u * 1024u * 1024u) {  // sanity cap (32 MB)
            LOGW("ClientSession[%s]: oversize message (%u)", peer_.c_str(), h.length);
            return false;
        }
        if (rx.size() - off < proto::kHeaderSize + h.length) break;  // need more
        handleMessage(h.type, rx.data() + off + proto::kHeaderSize, h.length);
        off += proto::kHeaderSize + h.length;
    }
    if (off) rx.erase(rx.begin(), rx.begin() + static_cast<std::ptrdiff_t>(off));
    return true;
}

void ClientSession::handleMessage(uint16_t type, const uint8_t* payload, uint32_t len) {
    const std::string body(reinterpret_cast<const char*>(payload), len);

    switch (static_cast<MsgType>(type)) {
        case MsgType::ServoCmd: {
            std::string mode = "step";
            double pan = 0.0, tilt = 0.0;
            mjson::getString(body, "mode", mode);
            mjson::getNumber(body, "pan", pan);
            mjson::getNumber(body, "tilt", tilt);
            Command c;
            c.type = (mode == "abs") ? CommandType::ServoAbs : CommandType::ServoStep;
            c.pan = pan;
            c.tilt = tilt;
            cmds_.push(c);
            break;
        }
        case MsgType::ModeCmd: {
            bool a = true;
            mjson::getBool(body, "autoTrack", a);
            Command c;
            c.type = CommandType::SetMode;
            c.autoTrack = a;
            cmds_.push(c);
            break;
        }
        case MsgType::FileListReq:
            sendFileList();
            break;
        case MsgType::FileGetReq: {
            std::string name;
            if (mjson::getString(body, "name", name)) sendFile(name);
            break;
        }
        case MsgType::FileDeleteReq: {
            std::string name;
            bool ok = false;
            if (mjson::getString(body, "name", name)) ok = files_.remove(name);
            sendJson(MsgType::FileDeleteResp,
                     "{\"name\":\"" + mjson::escape(name) +
                         "\",\"ok\":" + (ok ? "true" : "false") + "}");
            break;
        }
        case MsgType::SnapshotReq: {
            auto f = hub_.latest();
            if (f && !f->mat.empty()) files_.snapshot(f->mat);
            sendFileList();
            break;
        }
        case MsgType::Hello:
            LOGI("ClientSession[%s]: peer hello %s", peer_.c_str(), body.c_str());
            break;
        case MsgType::Heartbeat:
        default:
            break;
    }
}

bool ClientSession::sendFrameIfNew() {
    auto f = hub_.latest();
    if (!f || f->jpeg.empty()) return true;        // nothing captured yet
    if (f->seq == lastSentSeq_) return true;       // already sent this frame
    auto msg = proto::buildVideoFrame(f->seq, static_cast<uint32_t>(f->width),
                                      static_cast<uint32_t>(f->height), f->tsMs,
                                      f->jpeg.data(), f->jpeg.size());
    lastSentSeq_ = f->seq;
    return conn_->send(msg.data(), msg.size());
}

void ClientSession::sendJson(MsgType type, const std::string& json) {
    auto msg = proto::buildMessage(type, json);
    conn_->send(msg.data(), msg.size());  // a dead socket is caught by the frame loop
}

void ClientSession::sendFileList() {
    auto files = files_.list();
    std::string j = "[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i) j += ",";
        j += "{\"name\":\"" + mjson::escape(files[i].name) +
             "\",\"size\":" + std::to_string(files[i].size) +
             ",\"mtime\":" + std::to_string(files[i].mtime) + "}";
    }
    j += "]";
    sendJson(MsgType::FileListResp, j);
}

void ClientSession::sendFile(const std::string& name) {
    std::vector<uint8_t> bytes;
    const bool ok = files_.read(name, bytes);

    const std::size_t chunkSize = 32 * 1024;
    const uint32_t total = (!ok || bytes.empty())
        ? 0u
        : static_cast<uint32_t>((bytes.size() + chunkSize - 1) / chunkSize);

    auto buildChunk = [&](uint32_t idx, const uint8_t* p, std::size_t n) {
        std::vector<uint8_t> pl;
        proto::putU32(pl, idx);
        proto::putU32(pl, total);
        pl.push_back(static_cast<uint8_t>(name.size()));
        pl.insert(pl.end(), name.begin(), name.end());
        if (p && n) pl.insert(pl.end(), p, p + n);
        return proto::buildMessage(MsgType::FileData, pl.data(), pl.size());
    };

    if (total == 0) {  // not found / empty: single empty chunk signals "done, 0 chunks"
        auto msg = buildChunk(0, nullptr, 0);
        conn_->send(msg.data(), msg.size());
        return;
    }
    for (uint32_t idx = 0; idx < total; ++idx) {
        const std::size_t start = static_cast<std::size_t>(idx) * chunkSize;
        const std::size_t n = std::min(chunkSize, bytes.size() - start);
        auto msg = buildChunk(idx, bytes.data() + start, n);
        if (!conn_->send(msg.data(), msg.size())) return;
    }
}

} // namespace sec
