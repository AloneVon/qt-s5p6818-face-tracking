#include "FileManager.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>

#include "../core/Log.h"

namespace sec {

FileManager::FileManager(std::string dir, int jpegQuality)
    : dir_(std::move(dir)), jpegQuality_(jpegQuality) {}

bool FileManager::init() {
    if (::mkdir(dir_.c_str(), 0755) != 0 && errno != EEXIST) {
        LOGE("FileManager: cannot create dir '%s': %s", dir_.c_str(), std::strerror(errno));
        return false;
    }
    LOGI("FileManager: capture dir '%s'", dir_.c_str());
    return true;
}

// Reject anything that is not a plain basename (no separators, no traversal).
bool FileManager::resolve(const std::string& name, std::string& fullPath) const {
    if (name.empty() || name.size() > 128) return false;
    if (name.find('/') != std::string::npos) return false;
    if (name.find('\\') != std::string::npos) return false;
    if (name == "." || name == "..") return false;
    if (name.find("..") != std::string::npos) return false;
    for (char c : name)
        if (c == '\0' || static_cast<unsigned char>(c) < 0x20) return false;
    fullPath = dir_ + "/" + name;
    return true;
}

std::string FileManager::snapshot(const cv::Mat& bgr) {
    if (bgr.empty()) return "";
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char name[64];
    std::strftime(name, sizeof(name), "snap_%Y%m%d_%H%M%S.jpg", &tm);

    std::lock_guard<std::mutex> lk(m_);
    std::string full = dir_ + "/" + name;
    std::vector<int> params{ cv::IMWRITE_JPEG_QUALITY, jpegQuality_ };
    if (!cv::imwrite(full, bgr, params)) {
        LOGW("FileManager: imwrite failed for '%s'", full.c_str());
        return "";
    }
    LOGI("FileManager: saved snapshot '%s'", name);
    return name;
}

std::vector<FileInfo> FileManager::list() const {
    std::vector<FileInfo> out;
    std::lock_guard<std::mutex> lk(m_);
    DIR* d = ::opendir(dir_.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = ::readdir(d)) != nullptr) {
        if (e->d_name[0] == '.') continue;
        std::string full = dir_ + "/" + e->d_name;
        struct stat st{};
        if (::stat(full.c_str(), &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        out.push_back(FileInfo{ e->d_name,
                                static_cast<uint64_t>(st.st_size),
                                static_cast<uint64_t>(st.st_mtime) });
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(),
              [](const FileInfo& a, const FileInfo& b) { return a.mtime > b.mtime; });
    return out;
}

bool FileManager::read(const std::string& name, std::vector<uint8_t>& out) const {
    std::string full;
    if (!resolve(name, full)) { LOGW("FileManager: reject read '%s'", name.c_str()); return false; }
    std::lock_guard<std::mutex> lk(m_);
    std::FILE* f = std::fopen(full.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(sz));
    size_t rd = out.empty() ? 0 : std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    if (rd != out.size()) { out.clear(); return false; }
    return true;
}

bool FileManager::remove(const std::string& name) {
    std::string full;
    if (!resolve(name, full)) { LOGW("FileManager: reject delete '%s'", name.c_str()); return false; }
    std::lock_guard<std::mutex> lk(m_);
    if (::unlink(full.c_str()) != 0) {
        LOGW("FileManager: unlink '%s' failed: %s", full.c_str(), std::strerror(errno));
        return false;
    }
    LOGI("FileManager: deleted '%s'", name.c_str());
    return true;
}

} // namespace sec
