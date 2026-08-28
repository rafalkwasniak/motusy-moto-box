#include "RideHistory.h"

namespace motion {

const RideValues RideHistory::kEmpty{};

bool isEmptyRide(const RideValues& ride) {
    return ride.maxLeanLeftDeg <= 0.0f && ride.maxLeanRightDeg <= 0.0f &&
           ride.maxAccelG <= 0.0f && ride.maxBrakeG <= 0.0f && ride.maxSpeedKmh <= 0.0f;
}

bool RideHistory::push(const RideValues& ride) {
    if (isEmptyRide(ride)) return false;

    head_ = (head_ + kCapacity - 1) % kCapacity;
    slots_[head_] = ride;
    if (count_ < kCapacity) ++count_;
    return true;
}

const RideValues& RideHistory::at(size_t index) const {
    if (index >= count_) return kEmpty;
    return slots_[(head_ + index) % kCapacity];
}

void RideHistory::clear() {
    head_ = 0;
    count_ = 0;
    for (size_t i = 0; i < kCapacity; ++i) slots_[i] = RideValues{};
}

void RideHistory::restore(const RideValues* rides, size_t count) {
    clear();
    if (count > kCapacity) count = kCapacity;
    // Wpisy przychodza od najnowszego — wpychamy od najstarszego, zeby
    // kolejnosc w buforze sie zgadzala.
    for (size_t i = count; i > 0; --i) push(rides[i - 1]);
}

}  // namespace motion
