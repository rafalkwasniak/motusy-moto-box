#include "ImuSource.h"

#include <M5Unified.h>

namespace hal {
namespace {
/// Okno usredniania czestotliwosci probkowania.
constexpr unsigned long kRateWindowMs = 1000;
}  // namespace

bool ImuSource::begin() {
    ready_ = M5.Imu.isEnabled();

    // TODO(V5, architektura §1.1): ustawic jawnie zakresy +/-8 g i +/-1000 dps
    // oraz ODR 100 Hz. Do potwierdzenia, ktore z tych ustawien M5Unified
    // wystawia dla BMI270, a ktore wymagaja dostepu do rejestrow.

    rateWindowStartMs_ = millis();
    rateWindowSamples_ = 0;
    return ready_;
}

void ImuSource::trackSampleRate(unsigned long nowMs) {
    ++rateWindowSamples_;
    const unsigned long elapsed = nowMs - rateWindowStartMs_;
    if (elapsed < kRateWindowMs) return;

    sampleRateHz_ = static_cast<float>(rateWindowSamples_) * 1000.0f / static_cast<float>(elapsed);
    rateWindowStartMs_ = nowMs;
    rateWindowSamples_ = 0;
}

bool ImuSource::read(motion::ImuSample& out) {
    if (!ready_) return false;
    if (static_cast<int>(M5.Imu.update()) == 0) return false;

    const auto data = M5.Imu.getImuData();
    const unsigned long nowMs = millis();

    out.accelG = {data.accel.x, data.accel.y, data.accel.z};
    out.gyroRadS = {motion::degToRad(data.gyro.x),
                    motion::degToRad(data.gyro.y),
                    motion::degToRad(data.gyro.z)};
    out.timestampMs = nowMs;

    trackSampleRate(nowMs);
    return true;
}

}  // namespace hal
