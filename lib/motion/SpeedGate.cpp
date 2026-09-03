#include "SpeedGate.h"

namespace motion {

void SpeedGate::updateSpeed(float kmh, uint32_t nowMs) {
    haveSpeed_ = true;
    lastSpeedMs_ = nowMs;

    if (kmh >= config_.enterKmh) {
        open_ = true;
        below_ = false;
        return;
    }

    if (kmh < config_.exitKmh) {
        // Wybieg liczymy od PIERWSZEJ probki ponizej progu, nie od kazdej
        // kolejnej — inaczej hamowanie do zera odnawialoby go w nieskonczonosc.
        if (!below_) {
            below_ = true;
            belowSinceMs_ = nowMs;
        }
        if (coastExpired(nowMs)) open_ = false;
        return;
    }

    // Miedzy progami: histereza. Jadacy zostaje jadacym, stojacy stojacym,
    // a rozpoczety wybieg przerywamy — motocykl znowu sie toczy.
    below_ = false;
}

bool SpeedGate::isRecording(bool imuStationary, uint32_t nowMs) const {
    // Bez swiezej predkosci wracamy do reguly sprzed GPS-a.
    if (!hasFreshSpeed(nowMs)) return !imuStationary;

    if (!open_) return false;
    // Stan liczony takze przy odpytaniu, bo probki przychodza 1 Hz, a petla
    // pyta 100 razy na sekunde — koniec wybiegu ma nastapic co do chwili,
    // a nie dopiero przy nastepnym zdaniu z modulu.
    return !(below_ && coastExpired(nowMs));
}

bool SpeedGate::hasFreshSpeed(uint32_t nowMs) const {
    if (!haveSpeed_) return false;
    return nowMs - lastSpeedMs_ <= config_.fixHoldMs;
}

bool SpeedGate::coastExpired(uint32_t nowMs) const {
    return nowMs - belowSinceMs_ >= config_.coastMs;
}

void SpeedGate::reset() {
    open_ = false;
    below_ = false;
    belowSinceMs_ = 0;
    haveSpeed_ = false;
    lastSpeedMs_ = 0;
}

}  // namespace motion
