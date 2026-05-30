// VisionThread.h — T1. Owns the camera, captures frames, runs Haar detection,
// draws the overlay (face box + center crosshair), publishes the annotated
// frame to the FrameHub, and updates TrackingState + StatusBoard.
#pragma once

#include "../core/StoppableThread.h"
#include "../capture/ICamera.h"
#include "../vision/FaceDetector.h"
#include "../core/FrameHub.h"
#include "../core/TrackingState.h"
#include "../core/StatusBoard.h"

namespace sec {

class FileManager;  // full include in the .cpp (auto-snapshot)

class VisionThread : public StoppableThread {
public:
    VisionThread(ICamera& cam, FaceDetector& det, FrameHub& hub,
                 TrackingState& track, StatusBoard& status, FileManager& files,
                 int jpegQuality, bool autoSnapshot, int autoSnapshotIntervalMs);
    ~VisionThread() override { stop(); join(); }

protected:
    void run() override;

private:
    ICamera&       cam_;
    FaceDetector&  det_;
    FrameHub&      hub_;
    TrackingState& track_;
    StatusBoard&   status_;
    FileManager&   files_;
    int            jpegQuality_;
    bool           autoSnapshot_;
    int            autoSnapshotIntervalMs_;
};

} // namespace sec
