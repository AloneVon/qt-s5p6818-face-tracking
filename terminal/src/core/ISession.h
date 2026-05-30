// ISession.h — minimal interface the ClientRegistry needs, so core/ does not
// depend on net/. ClientSession (net/) implements it.
#pragma once

namespace sec {

class ISession {
public:
    virtual ~ISession() = default;
    virtual void requestStop() = 0;   // ask the session loop to exit
    virtual bool finished() const = 0; // true once the loop has returned
};

} // namespace sec
