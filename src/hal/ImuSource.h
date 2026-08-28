// Motusy Moto Box — zrodlo probek z BMI270.
//
// Cala rola tej klasy to zamiana API M5Unified na neutralna strukture
// motion::ImuSample. Dzieki temu warstwa algorytmiczna nie wie nic o M5Stack
// i kompiluje sie tez na komputerze.
//
// Jednostki: M5Unified oddaje przyspieszenie w g, a predkosc katowa w stopniach
// na sekunde. Algorytmy pracuja na radianach, wiec konwersja jest tutaj.

#pragma once

#include "Vec3.h"

namespace hal {

class ImuSource {
public:
    /// @return false gdy IMU nie odpowiada — urzadzenie powinno to pokazac
    ///         na ekranie startowym zamiast po cichu mierzyc zera.
    bool begin();

    bool isReady() const { return ready_; }

    /// @return true gdy pojawila sie nowa probka; `out` jest wtedy wypelnione.
    bool read(motion::ImuSample& out);

    /// Zmierzona czestotliwosc probkowania [Hz] — do diagnostyki.
    float sampleRateHz() const { return sampleRateHz_; }

private:
    void trackSampleRate(unsigned long nowMs);

    bool ready_ = false;
    float sampleRateHz_ = 0.0f;
    unsigned long rateWindowStartMs_ = 0;
    uint32_t rateWindowSamples_ = 0;
};

}  // namespace hal
