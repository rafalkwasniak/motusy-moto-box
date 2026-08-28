#include "AlarmEngine.h"

#include <algorithm>
#include <cmath>

namespace guard {
namespace {

/// Wygladzanie akcelerometru na potrzeby kata odniesienia. Stala niezalezna
/// od dt — przy 25-100 Hz daje stala czasowa rzedu ulamka sekundy, co wystarcza.
constexpr float kFilterAlpha = 0.2f;

/// Czestotliwosci sygnalizacji. Dwa tony na przemian w stopniu trzecim,
/// bo modulowany dzwiek zwraca uwage mocniej niz jednostajny pisk.
constexpr uint16_t kWarnFreqHz = 2500;
constexpr uint16_t kSirenLowHz = 2200;
constexpr uint16_t kSirenHighHz = 2900;

/// Czasy wzorcow (§20 zostawia je implementacji).
constexpr uint32_t kStage1BeepMs = 150;      // 3 krotkie piknieca
constexpr uint32_t kStage1TotalMs = 900;
constexpr uint32_t kStage2OnMs = 800;        // 3 dluzsze sygnaly
constexpr uint32_t kStage2CycleMs = 1100;
constexpr uint32_t kStage2TotalMs = 3300;
constexpr uint32_t kSirenWarbleMs = 250;     // przelaczanie tonow w stopniu 3

}  // namespace

void AlarmEngine::arm(const motion::Vec3& restingAccelG, uint32_t nowMs) {
    armed_ = true;
    reference_ = restingAccelG.normalized();
    filtered_ = restingAccelG;
    filterSeeded_ = true;

    conditionActive_ = false;
    violationLatched_ = false;
    violationCount_ = 0;
    quietSinceMs_ = nowMs;
    lastViolationMs_ = 0;

    signalling_ = false;
    signalStage_ = 0;
}

void AlarmEngine::disarm() {
    armed_ = false;
    signalling_ = false;
    violationCount_ = 0;
    conditionActive_ = false;
    violationLatched_ = false;
}

bool AlarmEngine::detectCondition(const motion::ImuSample& sample) {
    if (!filterSeeded_) {
        filtered_ = sample.accelG;
        filterSeeded_ = true;
    } else {
        filtered_ += (sample.accelG - filtered_) * kFilterAlpha;
    }

    // Kat miedzy wygladzonym wektorem grawitacji a pozycja odniesienia.
    const motion::Vec3 current = filtered_.normalized();
    const float dot = std::max(-1.0f, std::min(1.0f, current.dot(reference_)));
    const float tiltDeg = motion::radToDeg(std::acos(dot));

    // Zaburzenie modulu liczone z surowej probki — wygladzanie stlumiloby
    // wlasnie te szarpniecia, ktorych szukamy.
    const float magDeviation = std::fabs(sample.accelG.norm() - 1.0f);

    return tiltDeg > config_.tiltThresholdDeg || magDeviation > config_.accelThresholdG;
}

AlarmOutput AlarmEngine::update(const motion::ImuSample* sample, uint32_t nowMs) {
    AlarmOutput out;
    if (!armed_) return out;

    // ── Detekcja z warunkiem utrzymania ────────────────────────────────────
    if (sample != nullptr) {
        const bool condition = detectCondition(*sample);

        if (condition) {
            if (!conditionActive_) {
                conditionActive_ = true;
                conditionSinceMs_ = nowMs;
            }
            const bool sustained = nowMs - conditionSinceMs_ >= config_.sustainMs;
            const bool gapOk =
                lastViolationMs_ == 0 || nowMs - lastViolationMs_ >= config_.retriggerGapMs;

            if (sustained && !violationLatched_ && gapOk) {
                violationLatched_ = true;
                lastViolationMs_ = nowMs;
                quietSinceMs_ = nowMs;

                violationCount_ = static_cast<uint8_t>(
                    std::min<int>(violationCount_ + 1, config_.maxStage));

                out.violation = true;
                signalling_ = true;
                signalStage_ = violationCount_;
                patternStartMs_ = nowMs;
            }
        } else {
            conditionActive_ = false;
            violationLatched_ = false;
        }
    }

    // ── Wygaszanie eskalacji po ciszy ──────────────────────────────────────
    if (!conditionActive_ && violationCount_ > 0 &&
        nowMs - quietSinceMs_ >= config_.decayMs) {
        --violationCount_;
        quietSinceMs_ = nowMs;
    }

    // ── Wzorzec dzwiekowy ──────────────────────────────────────────────────
    if (signalling_) renderPattern(out, nowMs);

    out.stage = violationCount_;
    return out;
}

void AlarmEngine::renderPattern(AlarmOutput& out, uint32_t nowMs) {
    const uint32_t t = nowMs - patternStartMs_;

    switch (signalStage_) {
        case 1:
            // Trzy krotkie piknieca: gra w co drugim oknie 150 ms.
            if (t >= kStage1TotalMs) break;
            out.signalling = true;
            out.sirenOn = (t / kStage1BeepMs) % 2 == 0;
            out.freqHz = kWarnFreqHz;
            return;

        case 2:
            // Trzy dluzsze sygnaly.
            if (t >= kStage2TotalMs) break;
            out.signalling = true;
            out.sirenOn = (t % kStage2CycleMs) < kStage2OnMs;
            out.freqHz = kWarnFreqHz;
            return;

        default:
            // Stopien 3+: sygnal ciagly z twardym limitem czasu — potem
            // powrot do czuwania, zeby nie rozladowac baterii (§4.3 architektury).
            if (t >= config_.sirenCapMs) break;
            out.signalling = true;
            out.sirenOn = true;
            out.freqHz = (t / kSirenWarbleMs) % 2 == 0 ? kSirenLowHz : kSirenHighHz;
            return;
    }

    // Wzorzec dobiegl konca.
    signalling_ = false;
}

}  // namespace guard
