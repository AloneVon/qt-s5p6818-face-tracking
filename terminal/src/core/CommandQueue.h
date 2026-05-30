// CommandQueue.h — control commands from client sessions to the business thread.
// File operations are handled directly by the session via FileManager; only
// servo/mode commands flow through here. The business loop drains it each tick.
#pragma once

#include <deque>
#include <mutex>

namespace sec {

enum class CommandType {
    ServoStep,  // relative: pan/tilt are deltas in degrees
    ServoAbs,   // absolute: pan/tilt are target degrees
    SetMode,    // autoTrack on/off
};

struct Command {
    CommandType type = CommandType::ServoStep;
    double pan  = 0.0;
    double tilt = 0.0;
    bool   autoTrack = true;
};

class CommandQueue {
public:
    void push(const Command& c) {
        std::lock_guard<std::mutex> lk(m_);
        q_.push_back(c);
    }

    // Non-blocking pop; returns false if empty.
    bool tryPop(Command& out) {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return false;
        out = q_.front();
        q_.pop_front();
        return true;
    }

private:
    std::mutex m_;
    std::deque<Command> q_;
};

} // namespace sec
