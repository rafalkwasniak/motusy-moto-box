// Motusy Moto Box — widok diagnostyczny (widok serwisowy pod KEY1 hold).
//
// Nie jest czescia specyfikacji funkcjonalnej. Powstal, bo bez niego nie da sie
// sprawdzic na biurku, czy filtr dziala: pokazuje surowy stan estymatora,
// czestotliwosc probkowania i to, ktore korekcje sa aktywne.
//
// Przy montazu na motocyklu pozwala zweryfikowac kalibracje: przechylenie
// motocykla w lewo musi przesunac wskaznik w lewo.

#pragma once

#include <M5Unified.h>

#include "Orientation.h"
#include "ScreenBuffer.h"

namespace ui {

struct LiveViewModel {
    motion::OrientationState state;
    float sampleRateHz = 0.0f;
    bool imuOk = false;
    bool mountCalibrated = false;
};

class LiveView {
public:
    void draw(ScreenBuffer& buffer, const LiveViewModel& model);

private:
    static void drawHeader(m5gfx::LovyanGFX* gfx, const LiveViewModel& model);
    static void drawLeanIndicator(m5gfx::LovyanGFX* gfx, float rollDeg);
    static void drawReadouts(m5gfx::LovyanGFX* gfx, const LiveViewModel& model);
    static void drawFlags(m5gfx::LovyanGFX* gfx, const LiveViewModel& model);

    /// Zakres wskaznika przechylu w stopniach w kazda strone.
    static constexpr float kLeanScaleDeg = 60.0f;
};

}  // namespace ui
