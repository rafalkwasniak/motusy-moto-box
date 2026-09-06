#include "RideHistory.h"

namespace motion {

const RideValues RideHistory::kEmpty{};
const noise::RideNoise RideHistory::kNoNoise{};

bool isEmptyRide(const RideValues& ride) {
    return ride.maxLeanLeftDeg <= 0.0f && ride.maxLeanRightDeg <= 0.0f &&
           ride.maxAccelG <= 0.0f && ride.maxBrakeG <= 0.0f && ride.maxSpeedKmh <= 0.0f;
}

bool RideHistory::push(const RideValues& ride, uint32_t durationS, uint32_t recordedAt,
                       const noise::RideNoise& noise) {
    if (isEmptyRide(ride)) return false;

    head_ = (head_ + kCapacity - 1) % kCapacity;
    slots_[head_] = ride;
    durations_[head_] = durationS;
    recordedAt_[head_] = recordedAt;
    noise_[head_] = noise;
    if (count_ < kCapacity) ++count_;
    return true;
}

const RideValues& RideHistory::at(size_t index) const {
    if (index >= count_) return kEmpty;
    return slots_[(head_ + index) % kCapacity];
}

uint32_t RideHistory::durationAt(size_t index) const {
    if (index >= count_) return 0;
    return durations_[(head_ + index) % kCapacity];
}

uint32_t RideHistory::recordedAtAt(size_t index) const {
    if (index >= count_) return 0;
    return recordedAt_[(head_ + index) % kCapacity];
}

const noise::RideNoise& RideHistory::noiseAt(size_t index) const {
    if (index >= count_) return kNoNoise;
    return noise_[(head_ + index) % kCapacity];
}

void RideHistory::clear() {
    head_ = 0;
    count_ = 0;
    for (size_t i = 0; i < kCapacity; ++i) {
        slots_[i] = RideValues{};
        durations_[i] = 0;
        recordedAt_[i] = 0;
        noise_[i] = noise::RideNoise{};
    }
}

void RideHistory::restore(const RideValues* rides, const uint32_t* durations, size_t count,
                          const uint32_t* recordedAt, const noise::RideNoise* noise) {
    clear();
    if (count > kCapacity) count = kCapacity;
    // Wpisy przychodza od najnowszego — wpychamy od najstarszego, zeby
    // kolejnosc w buforze sie zgadzala.
    for (size_t i = count; i > 0; --i) {
        push(rides[i - 1], durations != nullptr ? durations[i - 1] : 0,
             recordedAt != nullptr ? recordedAt[i - 1] : 0,
             noise != nullptr ? noise[i - 1] : noise::RideNoise{});
    }
}

}  // namespace motion
