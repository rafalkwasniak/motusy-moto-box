#include "SegmentLean.h"

#include "Rounding.h"

namespace motion {
namespace {

float magnitude(float value) { return value < 0.0f ? -value : value; }

}  // namespace

void SegmentLean::update(float rollDeg) {
    const float candidate = magnitude(rollDeg);

    // TA SAMA regula, co dla rekordow przejazdu — jedno wyrazenie, jedne progi.
    if (!isCredible(candidate, config_.minLeanDeg, config_.maxLeanDeg)) return;

    if (candidate > magnitude(best_)) best_ = rollDeg;
}

int8_t SegmentLean::take() {
    const float value = best_;
    best_ = 0.0f;

    // Zaokraglenie przez motion::roundHalfUp, tak samo jak przechyl na ekranie
    // i w przesylce do API. Funkcja nie obsluguje wartosci ujemnych, wiec
    // zaokraglamy wartosc bezwzgledna i przywracamy znak.
    const int rounded = roundHalfUp(magnitude(value));
    return static_cast<int8_t>(value < 0.0f ? -rounded : rounded);
}

}  // namespace motion
