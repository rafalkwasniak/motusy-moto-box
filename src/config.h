// Motusy Moto Box — stale konfiguracyjne w jednym miejscu.
//
// Wszystko, co bedziemy stroic po jezdzie testowej, powinno trafic tutaj
// albo do struktur konfiguracyjnych w lib/motion, nie w glab kodu.

#pragma once

#include <cstdint>

#include "MountCalibration.h"

namespace cfg {

// ── Tozsamosc ──────────────────────────────────────────────────────────────
constexpr const char* kDeviceName = "MOTO BOX";
constexpr const char* kFirmwareVersion = "0.1.0-dev";

// ── Ekran ──────────────────────────────────────────────────────────────────
/// Obrot wyswietlacza. 1 i 3 to poziom 240x135, roznia sie o 180 stopni.
/// Wartosc 3 ustalona przy urzadzeniu trzymanym tak, jak bedzie zamontowane
/// na motocyklu (2026-08-28): przy 1 napisy stoja do gory nogami.
constexpr uint8_t kDisplayRotation = 3;
constexpr uint8_t kDisplayBrightness = 160;

// ── Czasy (specyfikacja funkcjonalna) ──────────────────────────────────────
/// §5 — ekran startowy widoczny przez 5 sekund.
constexpr uint32_t kSplashDurationMs = 5000;
/// §18 — okres oczekiwania po zaniku zasilania, przed uzbrojeniem alarmu.
/// Specyfikacja mowila o 3 minutach; skrocone do 2 na zyczenie (2026-08-28).
constexpr uint32_t kArmingDelayMs = 2UL * 60UL * 1000UL;

// Potwierdzanie zmiany stanu zasilania. Niesymetryczne: zanik potwierdzamy
// dluzej, bo rozruch silnika zapada napiecie na ulamek sekundy, a falszywy
// zanik uzbroilby alarm w trakcie jazdy.
constexpr uint32_t kPowerLossConfirmMs = 5000;
constexpr uint32_t kPowerReturnConfirmMs = 1500;

/// Po jakim czasie trzymania pokazac ekran wyboru akcji przycisku.
/// Krotkie klikniecie ma od razu dac efekt — bez migniecia "PRZELACZ ALARM",
/// ktore znika szybciej, niz da sie je przeczytac.
constexpr uint32_t kHoldPromptDelayMs = 500;
// Progi przycisku. Specyfikacja mowila o 3 s i 10 s, ale w rekach 10 sekund
// okazalo sie meczace — po testach na sprzecie (2026-08-28) pasma sa rowne,
// po 2 sekundy kazde:
//     do 2 s   -> przelaczenie alarmu
//     2 - 4 s  -> reset wynikow
//     od 4 s   -> kalibracja
/// §22.2 — prog resetu wynikow.
constexpr uint32_t kButtonResetHoldMs = 2000;
/// §22.3 — prog uruchomienia kalibracji.
constexpr uint32_t kButtonCalibrationHoldMs = 4000;

// ── Akwizycja IMU ──────────────────────────────────────────────────────────
/// Docelowa czestotliwosc probkowania. Dynamika motocykla miesci sie ponizej
/// 20 Hz, wiec 100 Hz daje komfortowy zapas — patrz architektura §8.
constexpr uint32_t kImuSampleIntervalMs = 10;

/// Odswiezanie ekranu. Rzadziej niz IMU, zeby nie kradlo czasu probkowaniu.
constexpr uint32_t kDisplayRefreshMs = 100;

// ── Pamiec nieulotna ───────────────────────────────────────────────────────
/// Odstep miedzy automatycznymi zapisami wynikow. Zapis przy kazdym nowym
/// rekordzie zajechalby flash — patrz architektura §6.2.
constexpr uint32_t kAutosaveIntervalMs = 30000;

// ── Montaz ─────────────────────────────────────────────────────────────────
/// Ktora os urzadzenia wskazuje przod motocykla. Kalibracja postojowa nie jest
/// w stanie tego wyznaczyc — patrz architektura §7.
constexpr motion::ForwardAxis kMountForwardAxis = motion::ForwardAxis::DeviceXPlus;

/// Ile probek usredniamy podczas kalibracji (§14).
constexpr uint16_t kCalibrationSampleCount = 200;

}  // namespace cfg
