#include "ServoController.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "../core/Log.h"

namespace sec {

ServoController::ServoController(std::string chipPath, int panChannel,
                                int tiltChannel, long periodNs, long minDutyNs,
                                long maxDutyNs)
    : chipPath_(std::move(chipPath)), panCh_(panChannel), tiltCh_(tiltChannel),
      periodNs_(periodNs), minDutyNs_(minDutyNs), maxDutyNs_(maxDutyNs) {}

ServoController::~ServoController() { release(); }

std::string ServoController::chanDir(int ch) const {
    return chipPath_ + "/pwm" + std::to_string(ch);
}

bool ServoController::writeStr(const std::string& path, const std::string& value) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    const bool ok = std::fputs(value.c_str(), f) >= 0;
    std::fclose(f);
    return ok;
}

bool ServoController::exportChannel(int ch) {
    const std::string dir = chanDir(ch);
    struct stat st{};
    if (::stat(dir.c_str(), &st) == 0) return true;  // already exported

    if (!writeStr(chipPath_ + "/export", std::to_string(ch))) {
        LOGE("Servo: export channel %d failed (check %s)", ch, chipPath_.c_str());
        return false;
    }
    // The pwm<ch> directory appears asynchronously (udev); wait briefly.
    for (int i = 0; i < 20; ++i) {
        if (::stat(dir.c_str(), &st) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOGE("Servo: channel %d dir '%s' did not appear", ch, dir.c_str());
    return false;
}

bool ServoController::configChannel(int ch) {
    const std::string dir = chanDir(ch);
    // duty must be <= period; zero it first so a period change can never be
    // rejected by leftover state from a previous run.
    writeStr(dir + "/duty_cycle", "0");
    if (!writeStr(dir + "/period", std::to_string(periodNs_))) {
        LOGE("Servo: set period on channel %d failed", ch);
        return false;
    }
    setChannelAngle(ch, 90.0);  // start centered
    if (!writeStr(dir + "/enable", "1")) {
        LOGE("Servo: enable channel %d failed", ch);
        return false;
    }
    return true;
}

long ServoController::angleToDutyNs(double angleDeg) const {
    angleDeg = std::clamp(angleDeg, 0.0, 180.0);
    return minDutyNs_ +
           static_cast<long>((angleDeg / 180.0) * (maxDutyNs_ - minDutyNs_));
}

void ServoController::setChannelAngle(int ch, double angleDeg) {
    writeStr(chanDir(ch) + "/duty_cycle", std::to_string(angleToDutyNs(angleDeg)));
}

bool ServoController::init() {
    if (!exportChannel(panCh_) || !exportChannel(tiltCh_)) return false;
    if (!configChannel(panCh_) || !configChannel(tiltCh_)) return false;
    ready_ = true;
    LOGI("Servo: ready (chip=%s pan=pwm%d tilt=pwm%d period=%ldns)",
         chipPath_.c_str(), panCh_, tiltCh_, periodNs_);
    return true;
}

void ServoController::setAngles(double panDeg, double tiltDeg) {
    if (!ready_) return;
    setChannelAngle(panCh_, panDeg);
    setChannelAngle(tiltCh_, tiltDeg);
}

void ServoController::release() {
    if (!ready_) return;
    writeStr(chanDir(panCh_) + "/enable", "0");
    writeStr(chanDir(tiltCh_) + "/enable", "0");
    ready_ = false;
}

} // namespace sec
