#include "RideMetrics.h"

#include <algorithm>

namespace motion {

void RideValues::raiseTo(const RideValues& other) {
    maxLeanLeftDeg = std::max(maxLeanLeftDeg, other.maxLeanLeftDeg);
    maxLeanRightDeg = std::max(maxLeanRightDeg, other.maxLeanRightDeg);
    maxAccelG = std::max(maxAccelG, other.maxAccelG);
    maxBrakeG = std::max(maxBrakeG, other.maxBrakeG);
    maxSpeedKmh = std::max(maxSpeedKmh, other.maxSpeedKmh);
}

bool RideMetrics::raiseRecord(float& record, float candidate, float minimum, float maximum) {
    if (!isCredible(candidate, minimum, maximum)) return false;
    if (candidate <= record) return false;
    record = candidate;
    return true;
}

void RideMetrics::update(const OrientationState& state) {
    const float rollDeg = state.rollDeg();
    const float longitudinalG = state.longitudinalG;

    bool changed = false;

    // Konwencja ukladu motocykla: roll dodatni = przechyl w prawo (§7 architektury).
    if (rollDeg < 0.0f) {
        const float leanLeft = -rollDeg;
        changed |= raiseRecord(currentRide_.maxLeanLeftDeg, leanLeft, config_.minLeanDeg, config_.maxLeanDeg);
        changed |= raiseRecord(overall_.maxLeanLeftDeg, leanLeft, config_.minLeanDeg, config_.maxLeanDeg);
    } else {
        changed |= raiseRecord(currentRide_.maxLeanRightDeg, rollDeg, config_.minLeanDeg, config_.maxLeanDeg);
        changed |= raiseRecord(overall_.maxLeanRightDeg, rollDeg, config_.minLeanDeg, config_.maxLeanDeg);
    }

    if (longitudinalG > 0.0f) {
        changed |= raiseRecord(currentRide_.maxAccelG, longitudinalG, config_.minAccelG, config_.maxAccelG);
        changed |= raiseRecord(overall_.maxAccelG, longitudinalG, config_.minAccelG, config_.maxAccelG);
    } else {
        const float braking = -longitudinalG;
        changed |= raiseRecord(currentRide_.maxBrakeG, braking, config_.minAccelG, config_.maxAccelG);
        changed |= raiseRecord(overall_.maxBrakeG, braking, config_.minAccelG, config_.maxAccelG);
    }

    dirty_ |= changed;
}

void RideMetrics::updateSpeed(const SpeedSample& speed) {
    if (!speed.valid) return;

    bool changed = false;
    changed |= raiseRecord(currentRide_.maxSpeedKmh, speed.kmh, config_.minSpeedKmh,
                           config_.maxSpeedKmh);
    changed |= raiseRecord(overall_.maxSpeedKmh, speed.kmh, config_.minSpeedKmh,
                           config_.maxSpeedKmh);
    dirty_ |= changed;
}

void RideMetrics::startNewRide() {
    // MAX OGOLNIE juz zawiera rekordy poprzedniej sesji — byly podnoszone
    // rownolegle w update() — wiec wystarczy wyzerowac sesje.
    currentRide_.clear();
    dirty_ = true;
}

void RideMetrics::resetAll() {
    overall_.clear();
    currentRide_.clear();
    dirty_ = true;
}

void RideMetrics::restore(const RideValues& overall, const RideValues& currentRide) {
    overall_ = overall;
    currentRide_ = currentRide;
    dirty_ = false;
}

}  // namespace motion
