#include "RideClock.h"

namespace motion {

void RideClock::restore(uint32_t seconds) {
    baseSeconds_ = seconds;
    firstMoveMs_ = 0;
    lastMoveMs_ = 0;
    started_ = false;
}

void RideClock::reset() {
    baseSeconds_ = 0;
    firstMoveMs_ = 0;
    lastMoveMs_ = 0;
    started_ = false;
}

void RideClock::update(bool moving, uint32_t nowMs) {
    if (!moving) return;

    if (!started_) {
        started_ = true;
        firstMoveMs_ = nowMs;
    }
    lastMoveMs_ = nowMs;
}

uint32_t RideClock::seconds() const {
    if (!started_) return baseSeconds_;

    // Odejmowanie na liczbach bez znaku daje poprawny wynik takze po
    // przekreceniu millis() (co 49 dni).
    const uint32_t elapsedMs = lastMoveMs_ - firstMoveMs_;
    return baseSeconds_ + elapsedMs / 1000;
}

}  // namespace motion
