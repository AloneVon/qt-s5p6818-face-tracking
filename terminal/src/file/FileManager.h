// FileManager.h — manages the on-disk capture directory (snapshots/recordings)
// that clients can list, download and delete over TCP. All filename handling is
// basename-only with a path-traversal guard so a client can never escape the dir.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace sec {

struct FileInfo {
    std::string name;
    uint64_t    size  = 0;
    uint64_t    mtime = 0;  // seconds since epoch
};

class FileManager {
public:
    FileManager(std::string dir, int jpegQuality);

    bool init();  // ensure the capture directory exists

    // Save a BGR frame as JPEG. Returns the basename on success, "" on failure.
    std::string snapshot(const cv::Mat& bgr);

    std::vector<FileInfo> list() const;                    // newest first
    bool read(const std::string& name, std::vector<uint8_t>& out) const;
    bool remove(const std::string& name);

    const std::string& dir() const { return dir_; }

private:
    // Validate a client-supplied name (basename only) and build the full path.
    bool resolve(const std::string& name, std::string& fullPath) const;

    std::string  dir_;
    int          jpegQuality_;
    mutable std::mutex m_;
};

} // namespace sec
