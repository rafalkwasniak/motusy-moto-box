// Motusy Moto Box — paleta i wymiary interfejsu.
//
// Ekran 1.14" ST7789P3, 135x240 natywnie, uzywany poziomo jako 240x135.
// Tlo jest czarne: logo ma przezroczyste otoczenie, wiec wtapia sie w tlo,
// a noca czarny ekran nie oslepia kierowcy.

#pragma once

#include <cstdint>

namespace ui {

/// Konwersja RGB888 -> RGB565. Wlasna, zeby stale byly constexpr niezaleznie
/// od wersji M5GFX.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace color {
constexpr uint16_t kBackground = rgb565(0, 0, 0);
constexpr uint16_t kPrimary = rgb565(255, 255, 255);
constexpr uint16_t kMuted = rgb565(120, 120, 128);
constexpr uint16_t kDivider = rgb565(48, 48, 54);

/// Stany urzadzenia — kolor paska statusu.
constexpr uint16_t kRiding = rgb565(80, 220, 120);
constexpr uint16_t kWaiting = rgb565(240, 190, 60);
constexpr uint16_t kAlarm = rgb565(240, 70, 70);
constexpr uint16_t kCalibration = rgb565(90, 170, 255);

/// Wyrozniki wartosci.
constexpr uint16_t kLean = rgb565(255, 255, 255);
constexpr uint16_t kAccel = rgb565(120, 210, 255);
constexpr uint16_t kBrake = rgb565(255, 150, 90);
constexpr uint16_t kSpeed = rgb565(150, 230, 190);
constexpr uint16_t kZero = rgb565(90, 90, 96);
}  // namespace color

namespace layout {
constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 135;

/// Oddech od krawedzi. Napisy dochodzace do samego brzegu wygladaja zle,
/// a na zaokraglonym szkle bywaja optycznie przyciete. Zaden widok nie powinien
/// rysowac tekstu poza obszarem [kContentLeft, kContentRight].
constexpr int kMarginX = 5;
constexpr int kMarginY = 3;
constexpr int kContentLeft = kMarginX;
constexpr int kContentRight = kScreenWidth - kMarginX;
constexpr int kContentWidth = kContentRight - kContentLeft;

/// Pasek statusu na gorze.
constexpr int kStatusBarHeight = 15;

/// Odznaki modulow w pasku statusu (ALM, GPX). Wypelniona = wlaczony,
/// sam obrys = wylaczony.
constexpr int kBadgeW = 34;
constexpr int kBadgeH = 13;
constexpr int kBadgeGap = 5;

/// Ile miejsca zostawiamy z prawej na wskaznik zasilania. Liczone pod
/// NAJSZERSZY wariant, czyli "100%" w Font2 (~37 px) — a nie pod "EXT", ktore
/// widac przy biurku i przy jezdzie, wiec latwo je wziac za przypadek typowy.
///
/// Odznaki sa dosuniete do tej rezerwy, a nie wysrodkowane w pasku: przy dwoch
/// odznakach wysrodkowana para siegalaby x=83, a najdluzszy napis stanu
/// ("BEZ OCHRONY") konczy sie w okolicy x=90. Kotwiczenie po prawej zostawia
/// lewej stronie ponad 110 px i nie zalezy od tego, ile znakow ma stan.
constexpr int kPowerReserveW = 44;

/// Naglowki kolumn.
constexpr int kHeaderY = 17;
constexpr int kHeaderHeight = 11;

/// Piec wierszy wartosci: LEWO, PRAWO, PRZYSP, HAMOW, PREDK.
/// 28 + 5*21 = 133, zostaja 2 px marginesu na dole.
constexpr int kRowTop = 28;
constexpr int kRowHeight = 21;
constexpr int kRowCount = 5;

/// Kolumny: etykieta | MAX OGOLNIE | OSTATNIA JAZDA.
/// Dwa dzielniki dobrane tak, zeby obie kolumny wartosci mialy niemal rowna
/// szerokosc (82 i 81 px), a etykiety zachowaly zapas na najdluzsza z nich.
constexpr int kLabelX = kContentLeft;
constexpr int kLabelDividerX = 72;
constexpr int kOverallRightX = 148;
constexpr int kRideRightX = kContentRight;
constexpr int kColumnDividerX = 154;
}  // namespace layout

}  // namespace ui
