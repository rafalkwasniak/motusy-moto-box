#include "Orientation.h"

#include <algorithm>

namespace motion {
namespace {

/// Wspolczynnik filtru dolnoprzepustowego pierwszego rzedu dla zadanej
/// czestotliwosci odciecia i kroku czasowego.
float lowPassAlpha(float cutoffHz, float dtSec) {
    if (cutoffHz <= 0.0f) return 1.0f;
    const float tau = 1.0f / (2.0f * kPi * cutoffHz);
    return dtSec / (tau + dtSec);
}

float clampf(float value, float lo, float hi) {
    return std::max(lo, std::min(hi, value));
}

/// Roznica katow sprowadzona do przedzialu [-pi, pi], zeby korekcja nigdy nie
/// dociagala estymaty "dluzsza droga" wokol okregu.
float angleDifference(float target, float current) {
    float diff = target - current;
    while (diff > kPi) diff -= 2.0f * kPi;
    while (diff < -kPi) diff += 2.0f * kPi;
    return diff;
}

}  // namespace

float Orientation::leanFromCoordinatedTurn(float speedMs, float verticalTurnRateRadS) {
    // W ustalonym zakrecie wypadkowa grawitacji i sily odsrodkowej lezy wzdluz osi
    // pionowej motocykla, co daje tan(phi) = v * omega / g.
    return std::atan2(speedMs * verticalTurnRateRadS, kGravity);
}

void Orientation::setSpeedHint(float speedMs, unsigned long timestampMs) {
    speedHintMs_ = speedMs;
    speedHintTimestampMs_ = timestampMs;
    speedHintValid_ = speedMs >= 0.0f;
}

void Orientation::resetAngles() {
    state_.rollRad = 0.0f;
    state_.pitchRad = 0.0f;
    state_.longitudinalG = 0.0f;
    state_.lateralG = 0.0f;
    initialized_ = false;
}

void Orientation::update(const ImuSample& sample, float dtSec) {
    if (!(dtSec > 0.0f)) return;
    dtSec = std::min(dtSec, config_.maxDtSec);

    // Offset zyroskopu jest wlasnoscia czujnika, wiec odejmujemy go jeszcze
    // w ukladzie urzadzenia, przed obrotem do ukladu motocykla.
    const Vec3 gyroCorrected = sample.gyroRadS - state_.gyroBiasRadS;

    const Vec3 accelBike = mount_.toBikeFrame(sample.accelG);
    const Vec3 rateBike = mount_.toBikeFrame(gyroCorrected);

    const float p = rateBike.x;  // przechylanie
    const float q = rateBike.y;  // pochylanie
    const float r = rateBike.z;  // odchylanie
    const float gyroMagnitude = rateBike.norm();

    // Pierwsza probka: zamiast calkowac od zera, przyjmujemy kat z akcelerometru.
    if (!initialized_) {
        state_.rollRad = std::atan2(-accelBike.y, -accelBike.z);
        state_.pitchRad = std::atan2(accelBike.x,
                                     std::sqrt(accelBike.y * accelBike.y + accelBike.z * accelBike.z));
        initialized_ = true;
    } else {
        integrateGyro(p, q, r, dtSec);
    }

    state_.accelCorrectionActive = false;
    state_.turnCorrectionActive = false;

    applyAccelCorrection(accelBike, gyroMagnitude, r, dtSec);
    applyTurnCorrection(q, r, sample.timestampMs, dtSec);
    updateStationary(accelBike, gyroMagnitude, sample.gyroRadS, sample.timestampMs, dtSec);

    state_.rollRad = clampf(state_.rollRad, -config_.maxPlausibleLeanRad, config_.maxPlausibleLeanRad);
    state_.pitchRad = clampf(state_.pitchRad, degToRad(-80.0f), degToRad(80.0f));

    updateOutputAccel(accelBike, dtSec);
}

void Orientation::integrateGyro(float p, float q, float r, float dtSec) {
    // Rownania kinematyki katow Eulera. tan(pitch) jest ograniczony, zeby
    // patologiczny odczyt nie wysadzil calkowania w poblizu +/-90 stopni.
    const float sinRoll = std::sin(state_.rollRad);
    const float cosRoll = std::cos(state_.rollRad);
    const float tanPitch = clampf(std::tan(state_.pitchRad), -5.0f, 5.0f);

    const float rollRate = p + (sinRoll * q + cosRoll * r) * tanPitch;
    const float pitchRate = cosRoll * q - sinRoll * r;

    state_.rollRad += rollRate * dtSec;
    state_.pitchRad += pitchRate * dtSec;
}

void Orientation::applyAccelCorrection(const Vec3& accelBike, float gyroMagnitude,
                                       float yawRate, float dtSec) {
    const float magnitude = accelBike.norm();

    // Trzy warunki naraz. Ustalony zakret ma modul przyspieszenia > 1 g ORAZ
    // znaczaca predkosc odchylania, wiec nie przejdzie przez te bramke —
    // i o to wlasnie chodzi.
    const bool magnitudeTrusted = std::fabs(magnitude - 1.0f) <= config_.accelTrustToleranceG;
    const bool rotationTrusted = gyroMagnitude <= config_.accelTrustMaxGyroRadS;
    const bool yawTrusted = std::fabs(yawRate) <= config_.accelTrustMaxYawRadS;

    if (!(magnitudeTrusted && rotationTrusted && yawTrusted)) return;

    const float rollFromAccel = std::atan2(-accelBike.y, -accelBike.z);
    const float pitchFromAccel = std::atan2(accelBike.x,
                                            std::sqrt(accelBike.y * accelBike.y + accelBike.z * accelBike.z));

    const float gain = clampf(config_.accelCorrectionGain * dtSec, 0.0f, 1.0f);
    state_.rollRad += angleDifference(rollFromAccel, state_.rollRad) * gain;
    state_.pitchRad += angleDifference(pitchFromAccel, state_.pitchRad) * gain;
    state_.accelCorrectionActive = true;
}

void Orientation::applyTurnCorrection(float q, float r, unsigned long nowMs, float dtSec) {
    if (!speedHintValid_) return;
    if (nowMs - speedHintTimestampMs_ > config_.speedHintMaxAgeMs) return;
    if (speedHintMs_ < config_.turnCorrectionMinSpeedMs) return;

    // Predkosc odchylania wokol PIONU SWIATA, nie wokol osi motocykla.
    // Przechylony motocykl skreca mieszanka predkosci q i r.
    const float cosPitch = std::cos(state_.pitchRad);
    if (std::fabs(cosPitch) < 0.1f) return;
    const float verticalTurnRate =
        (std::sin(state_.rollRad) * q + std::cos(state_.rollRad) * r) / cosPitch;

    const float rollFromTurn = leanFromCoordinatedTurn(speedHintMs_, verticalTurnRate);
    if (std::fabs(rollFromTurn) > config_.maxPlausibleLeanRad) return;

    const float gain = clampf(config_.turnCorrectionGain * dtSec, 0.0f, 1.0f);
    state_.rollRad += angleDifference(rollFromTurn, state_.rollRad) * gain;
    state_.turnCorrectionActive = true;
}

void Orientation::updateStationary(const Vec3& accelBike, float gyroMagnitude,
                                   const Vec3& rawGyroDevice, unsigned long nowMs, float dtSec) {
    const bool quiet = std::fabs(accelBike.norm() - 1.0f) <= config_.stationaryAccelToleranceG &&
                       gyroMagnitude <= config_.stationaryMaxGyroRadS;

    if (!quiet) {
        stationaryCandidate_ = false;
        state_.stationary = false;
        return;
    }

    if (!stationaryCandidate_) {
        stationaryCandidate_ = true;
        stationarySinceMs_ = nowMs;
    }

    if (nowMs - stationarySinceMs_ < config_.stationaryHoldMs) return;
    state_.stationary = true;

    // W bezruchu caly odczyt zyroskopu jest offsetem. Uczymy sie go powoli,
    // zeby jedna zaszumiona probka nie zepsula estymaty.
    const float alpha = clampf(dtSec / (config_.biasLearnTauSec + dtSec), 0.0f, 1.0f);
    state_.gyroBiasRadS += (rawGyroDevice - state_.gyroBiasRadS) * alpha;
}

void Orientation::updateOutputAccel(const Vec3& accelBike, float dtSec) {
    // Kompensacja grawitacji: przy uniesionym przodzie skladowa grawitacji
    // w osi jazdy udaje hamowanie. Patrz docs/architektura-techniczna.md §3.
    const float longitudinalRaw = accelBike.x - std::sin(state_.pitchRad);
    const float lateralRaw = accelBike.y + std::cos(state_.pitchRad) * std::sin(state_.rollRad);

    const float alpha = lowPassAlpha(config_.accelOutputCutoffHz, dtSec);
    state_.longitudinalG += (longitudinalRaw - state_.longitudinalG) * alpha;
    state_.lateralG += (lateralRaw - state_.lateralG) * alpha;
}

}  // namespace motion
