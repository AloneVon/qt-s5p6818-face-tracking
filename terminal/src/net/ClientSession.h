// ClientSession.h — one per connected client, runs on its own thread. A single
// thread multiplexes both directions with poll(): it pushes JPEG video frames
// at the configured rate and reads control/file messages as they arrive
// (matching "一个新线程负责两端的数据收发"). Servo/mode commands are forwarded
// to the business thread via CommandQueue; file ops go straight to FileManager.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../core/ISession.h"
#include "IConn.h"
#include "Protocol.h"

namespace sec {

class FrameHub;
class CommandQueue;
class FileManager;

class ClientSession : public ISession {
public:
    ClientSession(std::unique_ptr<IConn> conn, std::string peer, FrameHub& hub,
                  CommandQueue& cmds, FileManager& files, int streamFps,
                  std::string authToken = "");

    void run();  // blocking; executed on the session's own thread

    void requestStop() override { stop_.store(true); }
    bool finished() const override { return finished_.load(); }

private:
    bool readAndDispatch(std::vector<uint8_t>& rx);
    void handleMessage(uint16_t type, const uint8_t* payload, uint32_t len);
    bool sendFrameIfNew();
    void sendJson(proto::MsgType type, const std::string& json);
    void sendFileList();
    void sendFile(const std::string& name);

    std::unique_ptr<IConn> conn_;
    std::string  peer_;
    FrameHub&    hub_;
    CommandQueue& cmds_;
    FileManager& files_;
    int          streamFps_;
    std::string  authToken_;          // required token; empty = auth disabled
    bool         authed_;             // gates video + control until HELLO clears it
    uint32_t     lastSentSeq_ = UINT32_MAX;
    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{false};
};

} // namespace sec
