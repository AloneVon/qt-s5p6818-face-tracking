// StoppableThread.h — tiny base for the long-running worker threads (vision,
// display, business). Derived classes implement run() and must call stop()+join()
// in their own destructor (while their members are still alive).
#pragma once

#include <atomic>
#include <thread>

namespace sec {

class StoppableThread {
public:
    virtual ~StoppableThread() = default;

    void start() {
        if (running_.exchange(true)) return;
        th_ = std::thread([this] { run(); });
    }
    void stop() { running_.store(false); }
    void join() { if (th_.joinable()) th_.join(); }
    bool running() const { return running_.load(); }

protected:
    virtual void run() = 0;
    std::atomic<bool> running_{false};

private:
    std::thread th_;
};

} // namespace sec
