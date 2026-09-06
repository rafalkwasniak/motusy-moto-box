#include "AWeighting.h"

#include <cmath>

namespace noise {
namespace {

constexpr double kPi = 3.14159265358979323846;

/// Czestotliwosci narozne z IEC 61672-1. NIE zaokraglac.
constexpr double kF1 = 20.598997;
constexpr double kF2 = 107.65265;
constexpr double kF3 = 737.86223;
constexpr double kF4 = 12194.217;

/// Wzmocnienie normalizujace charakterystyke do 0 dB przy 1 kHz.
constexpr double kNormalizationDb = 1.9997;

}  // namespace

void AWeighting::configure(float sampleRateHz) {
    // c = 2 fs — wspolczynnik transformacji biliniowej.
    const double c = 2.0 * static_cast<double>(sampleRateHz);

    const double w1 = 2.0 * kPi * kF1;
    const double w2 = 2.0 * kPi * kF2;
    const double w3 = 2.0 * kPi * kF3;
    const double w4 = 2.0 * kPi * kF4;

    const double gain = w4 * w4 * std::pow(10.0, kNormalizationDb / 20.0);

    // Cztery zera w zerze rozdzielone miedzy sekcje: 2 + 1 + 1.
    // Szesc biegunow: 2 + 2 + 2. Wzmocnienie w calosci w pierwszej sekcji.

    // gain * s^2 / (s + w1)^2
    setSection(0, gain, 0.0, 0.0, 1.0, 2.0 * w1, w1 * w1, c);
    // s / ((s + w2)(s + w3))
    setSection(1, 0.0, 1.0, 0.0, 1.0, w2 + w3, w2 * w3, c);
    // s / (s + w4)^2
    setSection(2, 0.0, 1.0, 0.0, 1.0, 2.0 * w4, w4 * w4, c);

    reset();
}

void AWeighting::setSection(size_t index, double b2, double b1, double b0, double a2,
                            double a1, double a0, double c) {
    const double cc = c * c;

    const double B0 = b2 * cc + b1 * c + b0;
    const double B1 = 2.0 * (b0 - b2 * cc);
    const double B2 = b2 * cc - b1 * c + b0;

    const double A0 = a2 * cc + a1 * c + a0;
    const double A1 = 2.0 * (a0 - a2 * cc);
    const double A2 = a2 * cc - a1 * c + a0;

    Biquad& section = sections_[index];
    section.b0 = static_cast<float>(B0 / A0);
    section.b1 = static_cast<float>(B1 / A0);
    section.b2 = static_cast<float>(B2 / A0);
    section.a1 = static_cast<float>(A1 / A0);
    section.a2 = static_cast<float>(A2 / A0);
}

void AWeighting::reset() {
    for (size_t i = 0; i < kSections; ++i) sections_[i].reset();
}

float AWeighting::process(float x) {
    float y = x;
    for (size_t i = 0; i < kSections; ++i) y = sections_[i].process(y);
    return y;
}

}  // namespace noise
