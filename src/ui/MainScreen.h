// Motusy Moto Box — glowny ekran wynikow (§24 specyfikacji).
//
// Dziesiec wartosci w ukladzie dwoch kolumn:
//
//   ┌──────────────────────────────────────────┐
//   │ JAZDA              [ALM]           EXT   │  pasek statusu
//   ├──────────────────────────────────────────┤
//   │              MAX          LOTKA          │  naglowki kolumn
//   │ LEWE          42°           31°          │
//   │ PRAWE         38°           27°          │
//   │ WIOOO        0.63 g        0.55 g        │
//   │ PRRRR        0.82 g        0.71 g        │
//   │ MAX         187 km/h      164 km/h       │  bez pomiaru "---"
//   └──────────────────────────────────────────┘
//
// Rysowanie idzie przez sprite w PSRAM, zeby ekran nie migotal przy odswiezaniu.

#pragma once

#include <M5Unified.h>

#include "RideMetrics.h"
#include "ScreenBuffer.h"
#include "Theme.h"

namespace ui {

struct MainScreenModel {
    /// Dane lewej i prawej kolumny. Na ekranie glownym: MAX OGOLNIE i LOTKA;
    /// na stronach historii: dwa kolejne przejazdy.
    motion::RideValues overall;
    motion::RideValues ride;

    /// Naglowki kolumn. Historia podstawia numery przejazdow.
    const char* leftHeader = "MAX";
    const char* rightHeader = "LOTKA";

    /// Pusty slot historii rysuje sie kreskami zamiast zerami.
    bool leftPresent = true;
    bool rightPresent = true;

    const char* stateLabel = "JAZDA";
    uint16_t stateColor = color::kRiding;

    bool alarmEnabled = false;
    bool mountCalibrated = false;

    bool externalPower = false;
    int batteryPercent = 0;
};

class MainScreen {
public:
    void draw(ScreenBuffer& buffer, const MainScreenModel& model);

private:
    static void drawStatusBar(m5gfx::LovyanGFX* gfx, const MainScreenModel& model);
    static void drawHeaders(m5gfx::LovyanGFX* gfx, const MainScreenModel& model);
    static void drawRows(m5gfx::LovyanGFX* gfx, const MainScreenModel& model);

    /// Sformatowana wartosc wyrownana do prawej krawedzi kolumny.
    /// `unit` moze byc nullptr; `degreeMark` dorysowuje pierscien stopni.
    static void drawValue(m5gfx::LovyanGFX* gfx, int rightX, int centerY, const char* text,
                          const char* unit, bool degreeMark, uint16_t textColor);
};

}  // namespace ui
