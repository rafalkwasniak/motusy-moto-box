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

// Detekcja zasilania. Okno "tetna" ladowania w PowerSource filtruje zarowno
// migotanie isCharging() przy pelnej baterii, jak i zapady rozruchu — wiec
// potwierdzenie w maszynie stanow moze byc krotkie. Odliczanie jest
// antydatowane o sume obu czasow, przez co ekran gasnie rowno kArmingDelayMs
// po FAKTYCZNYM odlaczeniu zasilania.
// Okno 15 s to wartosc ostrozna — po odczycie "luki" z ekranu SPRZET mozna
// je zejsc do ~10 s (prog musi byc wyraznie wiekszy od zmierzonej luki).
constexpr uint32_t kChargePulseHoldMs = 15000;
constexpr uint32_t kPowerLossConfirmMs = 2000;
constexpr uint32_t kPowerReturnConfirmMs = 1500;

// Ekran obudzony na baterii gasnie sam — kazde zgasniecie ekranu przy
// wlaczonym module konczy sie ponownym uzbrojeniem alarmu.
constexpr uint32_t kWakeScreenMs = 30000;
/// Krotsze okno po wyciszeniu alarmu: czas na ewentualny drugi klik
/// (wylaczenie modulu), potem powrot do czuwania.
constexpr uint32_t kSilenceScreenMs = 15000;

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

// ── Alarm i dzwiek ─────────────────────────────────────────────────────────
/// Glosnosc sygnalizacji 0-255. Wbudowany glosnik 1 W jest zaskakujaco donosny.
constexpr uint8_t kSpeakerVolume = 255;
/// Odstep miedzy probkami IMU w czuwaniu — light sleep miedzy nimi [ms].
constexpr uint32_t kArmedSampleIntervalMs = 40;

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
