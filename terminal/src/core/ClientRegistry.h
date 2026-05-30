// ClientRegistry.h — owns the per-client session threads. The accept loop
// adopts each new (session, thread) pair here; reapFinished() joins+removes
// disconnected clients; stopAll() tears everything down at shutdown. Keeps the
// live client count in the StatusBoard for the display overlay.
#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "ISession.h"
#include "StatusBoard.h"

namespace sec {

class ClientRegistry {
public:
    explicit ClientRegistry(StatusBoard& status) : status_(status) {}

    ~ClientRegistry() { stopAll(); }

    void adopt(std::shared_ptr<ISession> session, std::thread th) {
        std::lock_guard<std::mutex> lk(m_);
        entries_.push_back(Entry{ std::move(session), std::move(th) });
        status_.clientCount.store(static_cast<int>(entries_.size()));
    }

    // Join and remove sessions whose loop has returned (client disconnected).
    void reapFinished() {
        std::lock_guard<std::mutex> lk(m_);
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->session->finished()) {
                if (it->th.joinable()) it->th.join();
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        status_.clientCount.store(static_cast<int>(entries_.size()));
    }

    void stopAll() {
        std::lock_guard<std::mutex> lk(m_);
        for (auto& e : entries_) e.session->requestStop();
        for (auto& e : entries_) if (e.th.joinable()) e.th.join();
        entries_.clear();
        status_.clientCount.store(0);
    }

    int count() const {
        std::lock_guard<std::mutex> lk(m_);
        return static_cast<int>(entries_.size());
    }

private:
    struct Entry {
        std::shared_ptr<ISession> session;
        std::thread th;
    };
    mutable std::mutex m_;
    std::vector<Entry> entries_;
    StatusBoard& status_;
};

} // namespace sec
