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

// Detekcja zasilania opiera sie na napieciu wejsciowym mierzonym przez PMIC
// (5,21 V z kablem, 0 V bez — zmierzone 2026-08-29), wiec jest natychmiastowa.
// Zostaje samo potwierdzenie czasowe, niesymetryczne: zanik potwierdzamy
// dluzej, bo rozruch silnika zapada napiecie na ulamek sekundy, a falszywy
// zanik uzbroilby alarm w trakcie jazdy.
constexpr uint32_t kPowerLossConfirmMs = 2000;
constexpr uint32_t kPowerReturnConfirmMs = 1500;

/// Fallback dla plytek nieoddajacych napiecia wejsciowego — patrz PowerSource.
constexpr uint32_t kChargePulseHoldMs = 15000;

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
//     2 - 4 s  -> slad trasy GPX
//     4 - 6 s  -> reset wynikow
//     6 - 8 s  -> kalibracja
//     od 8 s   -> integracja ze strona
//
// KOLEJNOSC WEDLUG CZESTOSCI UZYCIA (decyzja uzytkownika, 2026-09-04): alarm
// i slad to przelaczniki uzywane regularnie, wiec leza najplycej. Reset jest
// sporadyczny, bo zeruje takze rekord predkosci. Kalibracja i integracja to
// akcje jednorazowe — osiem sekund jest tam kosztem przyjetym swiadomie,
// inaczej niz przy akcji, do ktorej wraca sie co jazde.
/// Prog przelaczenia zapisu sladu trasy.
constexpr uint32_t kButtonTrackHoldMs = 2000;
/// §22.2 — prog resetu wynikow.
constexpr uint32_t kButtonResetHoldMs = 4000;
/// §22.3 — prog uruchomienia kalibracji.
constexpr uint32_t kButtonCalibrationHoldMs = 6000;
/// Prog konfiguracji integracji. Ostatnia pozycja celowo: wchodzi sie tu raz,
/// przy uruchamianiu urzadzenia, a potem sie o niej zapomina. Droga przez
/// slad, reset i kalibracje jest bezpieczna — §23 wykonuje wylacznie akcje
/// progu osiagnietego w chwili puszczenia.
constexpr uint32_t kButtonIntegrationHoldMs = 8000;

/// Jak dlugo ekran INTEGRACJA czeka, zanim sam wroci do wynikow. Pieć minut,
/// bo w tym czasie uzytkownik szuka telefonu, laczy sie z siecia urzadzenia
/// i przepisuje token — trzydziesci sekund starczyloby na samo przeczytanie
/// hasla. Odliczanie rusza od nowa, dopoki ktos jest polaczony z formularzem.
constexpr uint32_t kIntegrationScreenMs = 5UL * 60UL * 1000UL;

// ── Integracja ze strona ───────────────────────────────────────────────────
// Adres ustalony 2026-09-01: API stoi na glownej domenie, nie na subdomenie
// motobox (ta nie istnieje w DNS). Certyfikat Let's Encrypt obejmuje motusy.top
// — patrz src/net/RootCert.h.
constexpr const char* kApiPingUrl = "https://motusy.top/api/v1/ping";
constexpr const char* kApiRidesUrl = "https://motusy.top/api/v1/rides";

/// Ile czekamy na polaczenie z siecia domowa. Router w garazu bywa daleko,
/// ale po pietnastu sekundach kolejne czekanie to juz tylko palenie pradu.
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
/// Timeout pojedynczego zadania HTTP.
constexpr uint32_t kHttpTimeoutMs = 10000;

// ── Alarm i dzwiek ─────────────────────────────────────────────────────────
/// Glosnosc sygnalizacji 0-255. Wbudowany glosnik 1 W jest zaskakujaco donosny.
constexpr uint8_t kSpeakerVolume = 255;
/// Odstep miedzy probkami IMU przy UZBROJONYM alarmie [ms] — kompromis
/// miedzy zuzyciem a czasem reakcji na ruch.
constexpr uint32_t kArmedSampleIntervalMs = 40;

/// Odstep miedzy wybudzeniami przy WYLACZONYM alarmie [ms]. Nie ma wtedy
/// czego probkowac — wystarczy zajrzec, czy nie wrocilo zasilanie.
/// Budzenie 25 razy na sekunde bylo w tym stanie czysta strata.
constexpr uint32_t kIdleWakeIntervalMs = 1000;

// ── Akwizycja IMU ──────────────────────────────────────────────────────────
/// Docelowa czestotliwosc probkowania. Dynamika motocykla miesci sie ponizej
/// 20 Hz, wiec 100 Hz daje komfortowy zapas — patrz architektura §8.
constexpr uint32_t kImuSampleIntervalMs = 10;

/// Odswiezanie ekranu. Rzadziej niz IMU, zeby nie kradlo czasu probkowaniu.
constexpr uint32_t kDisplayRefreshMs = 100;

// ── Modul GPS (Grove Port A) ───────────────────────────────────────────────
// Architektura §2.5. Piny i predkosc transmisji to WARTOSCI WYJSCIOWE, nie
// pewniki: kolejnosc zyl w kablu Grove i fabryczne ustawienie modulu
// rozstrzyga sie dopiero na sprzecie, wiec hal::GpsSource sprawdza po kolei
// obie kolejnosci pinow i obie predkosci.
/// Grove Port A na StickS3: SCL=G10, SDA=G9 (tabela pinow w M5Unified).
/// Lista sprawdzanych predkosci transmisji siedzi w hal/GpsSource.cpp — jest
/// czescia procedury dobierania portu, nie ustawieniem do strojenia.
constexpr int kGpsRxPin = 10;
constexpr int kGpsTxPin = 9;
/// Dwie sekundy na probe = dwie szanse przy nadawaniu 1 Hz.
constexpr uint32_t kGpsProbeMs = 2000;
/// Ile fix pozostaje wazny bez potwierdzenia. Krotki tunel nie moze kasowac
/// predkosci z ekranu, ale dziesiec sekund starych danych to juz zmyslanie.
constexpr uint32_t kGpsFixMaxAgeMs = 5000;
/// Progi jakosci fixa — ponizej nich predkosc nie jest pomiarem (§3a).
constexpr uint8_t kGpsMinSatellites = 4;
constexpr float kGpsMaxHdop = 5.0f;

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
