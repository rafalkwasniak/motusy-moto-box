// Motusy Moto Box — filtr wazenia A wedlug IEC 61672-1.
//
// Ucho nie slyszy wszystkich czestotliwosci jednakowo: 100 Hz o tym samym
// cisnieniu akustycznym co 1 kHz brzmi wyraznie ciszej. Wazenie A odwzorowuje
// te nierownosc, zeby zmierzona liczba mowila cos o halasie, a nie o energii.
//
// PRZENIESIONE 1:1 z DBMeterV1 — ani jedna cyfra nie jest tu do strojenia.
// To jest PRZELICZNIK, nie kalibracja: dwie rozne sztuki sprzetu maja rozne
// stale kalibracyjne, ale ten sam filtr. Patrz docs/pomiar-halasu.md §2.
//
//                  K · s^4
//   H(s) = ─────────────────────────────
//          (s+w1)^2 (s+w2) (s+w3) (s+w4)^2
//
//   f1 =    20.598997 Hz      f3 =   737.86223 Hz
//   f2 =   107.65265  Hz      f4 = 12194.217   Hz
//   K  = w4^2 · 10^(1.9997/20)      <- normalizacja do 0 dB przy 1 kHz
//
// Rozbite na trzy sekcje biquad i przeniesione do dziedziny cyfrowej
// transformacja biliniowa.
//
// DLACZEGO 48 kHz, A NIE MNIEJ. Transformacja biliniowa deformuje
// charakterystyke tym mocniej, im blizej Nyquista. Blad przy 8 kHz (gorna
// granica pasma wymaganego przez norme) to 0,54 dB przy 48 kHz, ale 1,47 dB
// przy 32 kHz. Motocykl ma sporo energii wysoko (ssanie, lancuch, wydech),
// wiec to nie jest akademickie. Prewarpingu NIE stosujemy — te same liczby
// co w pierwszym urzadzeniu sa wazniejsze niz pol decybela na skraju pasma.
//
// Wspolczynniki licza sie w double RAZ, przy configure(), a pracuja jako
// float. ESP32-S3 nie ma sprzetowego double, wiec liczenie w nim 48 tysiecy
// razy na sekunde byloby nie do udzwigniecia.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>

namespace noise {

class AWeighting {
public:
    explicit AWeighting(float sampleRateHz = 48000.0f) { configure(sampleRateHz); }

    /// Przelicza wspolczynniki dla zadanej czestotliwosci probkowania
    /// i zeruje stan filtru.
    void configure(float sampleRateHz);

    /// Zeruje wylacznie stan, zostawia wspolczynniki.
    void reset();

    /// Jedna probka znormalizowana do <-1, 1>.
    float process(float x);

private:
    /// Sekcja drugiego rzedu, postac bezposrednia II transponowana.
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1 = 0.0f; z2 = 0.0f; }
    };

    /// Transformacja biliniowa sekcji analogowej (b2 s^2 + b1 s + b0)
    /// / (a2 s^2 + a1 s + a0) przy s -> c (1 - z^-1) / (1 + z^-1).
    void setSection(size_t index, double b2, double b1, double b0, double a2, double a1,
                    double a0, double c);

    static constexpr size_t kSections = 3;
    Biquad sections_[kSections]{};
};

}  // namespace noise
