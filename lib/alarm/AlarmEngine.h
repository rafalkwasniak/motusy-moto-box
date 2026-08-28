// Motusy Moto Box — silnik alarmu: detekcja ruchu i eskalacja sygnalizacji.
//
// Realizuje §19 i §20 specyfikacji jako czyste C++ bez zaleznosci od sprzetu.
// Warstwa sprzetowa dostaje z kazdego kroku gotowa decyzje: czy glosnik ma grac
// i jakim tonem — sama nie podejmuje zadnych decyzji.
//
// DETEKCJA (§19). W momencie uzbrojenia zapamietujemy wektor grawitacji jako
// pozycje odniesienia. Naruszeniem jest:
//   - odchylenie kata od pozycji odniesienia (ktos zdejmuje motocykl ze stopki,
//     przesuwa go, laduje na lawete) — najpewniejszy sygnal,
//   - zaburzenie modulu przyspieszenia (szarpniecie, popchniecie, uderzenie).
// Warunek musi utrzymac sie przez `sustainMs` — pojedyncze drgniecie od
// przejezdzajacej ciezarowki nie eskaluje alarmu (§19 wymaga tego wprost).
//
// Detekcja pracuje na surowych danych z akcelerometru W UKLADZIE URZADZENIA —
// kalibracja montazu nie jest potrzebna, bo odniesieniem jest pozycja
// z momentu uzbrojenia, nie uklad motocykla.
//
// ESKALACJA (§20). Trzy stopnie:
//   1. pierwsze naruszenie      -> krotkie piknieca ostrzegawcze
//   2. kolejne naruszenie       -> dluzsze sygnaly
//   3. trzecie i dalsze         -> sygnal ciagly z twardym limitem czasu
// Limit czasu jest krytyczny: wzmacniacz 1 W przy ciaglym dzwieku ciagnie
// setki mA — alarm, ktory rozladuje baterie, przestaje pilnowac motocykla.
// Po cichej minucie poziom eskalacji opada o stopien.

#pragma once

#include <cstdint>

#include "Vec3.h"

namespace guard {

struct AlarmConfig {
    /// Odchylenie od pozycji odniesienia uznawane za ruch [stopnie].
    float tiltThresholdDeg = 4.0f;
    /// Zaburzenie modulu przyspieszenia uznawane za ruch [g].
    float accelThresholdG = 0.12f;
    /// Jak dlugo warunek musi sie utrzymac, zanim zglosimy naruszenie [ms].
    uint32_t sustainMs = 250;
    /// Minimalny odstep miedzy kolejnymi zliczonymi naruszeniami [ms] —
    /// jedno dluzsze szarpniecie to jedno naruszenie, nie dziesiec.
    uint32_t retriggerGapMs = 2000;
    /// Po jakim czasie ciszy poziom eskalacji opada o stopien [ms].
    uint32_t decayMs = 60000;
    /// Twardy limit ciaglej sygnalizacji trzeciego stopnia [ms].
    uint32_t sirenCapMs = 30000;
    /// Najwyzszy dozwolony stopien — przy slabej baterii warstwa aplikacji
    /// moze go obnizyc, zeby zachowac zdolnosc czuwania.
    uint8_t maxStage = 3;
};

/// Decyzja dla warstwy sprzetowej — wynik kazdego kroku.
struct AlarmOutput {
    /// Nowe naruszenie wykryte w tym kroku (zdarzenie, nie stan) —
    /// to jest sygnal `motionDetected` dla maszyny stanow urzadzenia.
    bool violation = false;
    /// Czy trwa sygnalizacja dzwiekowa.
    bool signalling = false;
    /// Czy glosnik ma grac w tej chwili (wzorzec przerywany!).
    bool sirenOn = false;
    /// Czestotliwosc tonu [Hz], wazna gdy sirenOn.
    uint16_t freqHz = 0;
    /// Biezacy stopien eskalacji 0-3.
    uint8_t stage = 0;
};

class AlarmEngine {
public:
    explicit AlarmEngine(const AlarmConfig& config = {}) : config_(config) {}

    const AlarmConfig& config() const { return config_; }
    void setConfig(const AlarmConfig& config) { config_ = config; }

    /// Uzbrojenie: `restingAccelG` to aktualny odczyt akcelerometru (pozycja
    /// odniesienia). Zeruje licznik eskalacji.
    void arm(const motion::Vec3& restingAccelG, uint32_t nowMs);

    /// Rozbrojenie: milknie natychmiast, licznik eskalacji wyzerowany.
    void disarm();

    bool isArmed() const { return armed_; }

    /// Krok silnika. `sample` moze byc nullptr (brak nowej probki — sam uplyw
    /// czasu tez zmienia stan wzorca dzwiekowego).
    AlarmOutput update(const motion::ImuSample* sample, uint32_t nowMs);

    /// Aktualny licznik naruszen (dla diagnostyki).
    uint8_t violationCount() const { return violationCount_; }

private:
    bool detectCondition(const motion::ImuSample& sample);
    void renderPattern(AlarmOutput& out, uint32_t nowMs);

    AlarmConfig config_{};

    bool armed_ = false;
    motion::Vec3 reference_;
    motion::Vec3 filtered_;
    bool filterSeeded_ = false;

    bool conditionActive_ = false;
    uint32_t conditionSinceMs_ = 0;
    bool violationLatched_ = false;
    uint32_t lastViolationMs_ = 0;

    uint8_t violationCount_ = 0;
    uint32_t quietSinceMs_ = 0;

    bool signalling_ = false;
    uint8_t signalStage_ = 0;
    uint32_t patternStartMs_ = 0;
};

}  // namespace guard
