// Motusy Moto Box — dwa niezalezne zestawy wynikow maksymalnych.
//
// Patrz specyfikacja funkcjonalna §8-§12.
//
// MAX OGOLNIE     — rekordy od ostatniego recznego wyzerowania
// OSTATNIA JAZDA  — rekordy biezacej sesji, zerowane przy kazdym wlaczeniu stacyjki

#pragma once

#include "Orientation.h"

namespace motion {

/// Wartosci maksymalne. Wszystkie sa nieujemne — hamowanie przechowujemy
/// jako modul, zeby porownanie "czy to nowy rekord" bylo zwyklym `>`.
/// Znak minus dokladamy dopiero przy prezentacji (HAMOWANIE: -0.82 g).
struct RideValues {
    float maxLeanLeftDeg = 0.0f;
    float maxLeanRightDeg = 0.0f;
    float maxAccelG = 0.0f;
    float maxBrakeG = 0.0f;

    /// Predkosc maksymalna [km/h]. Zrodlem jest GPS — bez modulu pozostaje 0
    /// i warstwa prezentacji pokazuje "---" zamiast zera.
    float maxSpeedKmh = 0.0f;

    void clear() { *this = RideValues{}; }

    /// Podnosi kazda wartosc do maksimum z obu zestawow. Uzywane przy
    /// przenoszeniu rekordow sesji do MAX OGOLNIE.
    void raiseTo(const RideValues& other);
};

/// Odczyt predkosci z GPS. `valid` musi byc ustawione przez warstwe parsujaca
/// NMEA dopiero po potwierdzeniu fixa — samo odebranie zdania to za malo.
struct SpeedSample {
    float kmh = 0.0f;
    bool valid = false;
};

/// Okno wiarygodnosci pomiaru: ponizej minimum to szum estymaty, powyzej
/// maksimum blad estymacji albo uderzenie. JEDNA REGULA dla rekordow przejazdu
/// i dla sladu trasy — gdy te dwie sciezki maja wlasne progi, uzytkownik widzi
/// "lewo 8 stopni" w wyniku i -31 w sladzie, i nie ma jak rozstrzygnac, ktora
/// liczba klamie. Ten sam powod, dla ktorego zaokraglanie ma jedno zrodlo
/// w motion::Rounding.h.
constexpr bool isCredible(float candidate, float minimum, float maximum) {
    return candidate >= minimum && candidate <= maximum;
}

struct RideMetricsConfig {
    /// Ponizej tego progu wartosci traktujemy jako szum i nie zapisujemy
    /// jako rekordow — inaczej postoj na swiatlach ustanowilby "rekord" 0.4 stopnia.
    float minLeanDeg = 3.0f;
    float minAccelG = 0.05f;
    /// Odbiornik GPS na postoju potrafi pokazywac kilka km/h szumu.
    float minSpeedKmh = 5.0f;

    /// Gorne granice wiarygodnosci. Przekroczenie oznacza blad estymacji,
    /// uderzenie albo bledny fix GPS, nie rzeczywisty wyczyn.
    float maxLeanDeg = 60.0f;
    float maxAccelG = 2.0f;
    float maxSpeedKmh = 400.0f;
};

class RideMetrics {
public:
    explicit RideMetrics(const RideMetricsConfig& config = {}) : config_(config) {}

    void setConfig(const RideMetricsConfig& config) { config_ = config; }

    /// Aktualizuje oba zestawy na podstawie biezacej estymaty orientacji.
    void update(const OrientationState& state);

    /// Aktualizuje rekord predkosci. Wywolywane w rytmie GPS (1-10 Hz),
    /// niezaleznie od petli IMU. Probka bez fixa jest ignorowana.
    void updateSpeed(const SpeedSample& speed);

    /// Nowa sesja jazdy: zeruje OSTATNIA JAZDA, nie rusza MAX OGOLNIE (§11).
    void startNewRide();

    /// Reczny reset (§15): zeruje oba zestawy. Nie dotyka kalibracji ani alarmu.
    void resetAll();

    const RideValues& overall() const { return overall_; }
    const RideValues& currentRide() const { return currentRide_; }

    void restore(const RideValues& overall, const RideValues& currentRide);

    /// Czy od ostatniego `clearDirty()` cokolwiek sie zmienilo. Sterowanie
    /// zapisem do NVS — patrz docs/architektura-techniczna.md §6.2.
    bool dirty() const { return dirty_; }
    void clearDirty() { dirty_ = false; }

private:
    bool raiseRecord(float& record, float candidate, float minimum, float maximum);

    RideMetricsConfig config_{};
    RideValues overall_{};
    RideValues currentRide_{};
    bool dirty_ = false;
};

}  // namespace motion
