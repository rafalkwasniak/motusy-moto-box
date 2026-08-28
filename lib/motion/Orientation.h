// Motusy Moto Box — estymacja orientacji motocykla z IMU (+ opcjonalnie predkosc).
//
// Patrz docs/architektura-techniczna.md §2 i §3.
//
// SEDNO PROBLEMU: motocykl w ustalonym zakrecie jest w rownowadze, wiec
// akcelerometr mierzy wypadkowa grawitacji i sily odsrodkowej wzdluz wlasnej osi
// pionowej — i pokazuje przechyl 0 stopni niezaleznie od rzeczywistego przechylu.
// Klasyczny filtr komplementarny ze stalym wzmocnieniem "wyprostowalby" motocykl
// w kazdym zakrecie.
//
// Dlatego przechyl pochodzi z calkowania zyroskopu, a korekcje sa BRAMKOWANE:
//
//   1. Korekcja akcelerometrem — tylko gdy odczyt jest wiarygodny (modul ~1 g,
//      male predkosci katowe). W zakrecie bramka jest zamknieta.
//   2. Korekcja z ustalonego zakretu — wymaga predkosci (GPS). Dziala WLASNIE
//      w zakrecie, wiec uzupelnia bramke nr 1. Bez predkosci nieaktywna.
//   3. Estymacja offsetu zyroskopu w spoczynku.

#pragma once

#include "MountCalibration.h"
#include "Vec3.h"

namespace motion {

/// Parametry strojenia. Wartosci domyslne sa punktem startowym przed pierwsza
/// jazda testowa — po analizie nagrania CSV nalezy je zweryfikowac.
struct OrientationConfig {
    // ── Bramka korekcji akcelerometrem ─────────────────────────────────────
    /// Dopuszczalne odchylenie modulu przyspieszenia od 1 g. W zakrecie modul
    /// rosnie (przy 40 stopniach przechylu do ~1.3 g), co zamyka bramke.
    float accelTrustToleranceG = 0.10f;
    /// Maksymalna predkosc katowa, przy ktorej ufamy akcelerometrowi.
    float accelTrustMaxGyroRadS = degToRad(15.0f);
    /// Maksymalna predkosc odchylania. Ustalony zakret ma male tempo przechylania,
    /// ale duza predkosc odchylania — to ja odroznia zakret od jazdy na wprost.
    float accelTrustMaxYawRadS = degToRad(5.0f);
    /// Tempo dociagania estymaty do kata z akcelerometru [1/s].
    float accelCorrectionGain = 0.6f;

    // ── Wykrywanie spoczynku i nauka offsetu zyroskopu ──────────────────────
    float stationaryAccelToleranceG = 0.05f;
    float stationaryMaxGyroRadS = degToRad(1.5f);
    /// Jak dlugo warunki musza sie utrzymac, zanim uznamy spoczynek [ms].
    unsigned long stationaryHoldMs = 500;
    /// Stala czasowa uczenia offsetu zyroskopu w spoczynku [s].
    float biasLearnTauSec = 2.0f;

    // ── Korekcja z ustalonego zakretu (wymaga predkosci z GPS) ──────────────
    /// Ponizej tej predkosci zaleznosc tan(phi) = v*omega/g nie ma sensu [m/s].
    float turnCorrectionMinSpeedMs = 5.0f;
    float turnCorrectionGain = 0.8f;
    /// Po jakim czasie odczyt predkosci uznajemy za nieaktualny [ms].
    unsigned long speedHintMaxAgeMs = 1500;

    // ── Filtracja wyjscia ──────────────────────────────────────────────────
    /// Czestotliwosc odciecia filtru przyspieszen [Hz]. Odcina wibracje silnika
    /// i pojedyncze uderzenia na nierownosciach.
    float accelOutputCutoffHz = 5.0f;

    // ── Zabezpieczenia ─────────────────────────────────────────────────────
    /// Fizycznie nieosiagalny przechyl. Przekroczenie oznacza blad estymacji.
    float maxPlausibleLeanRad = degToRad(65.0f);
    float maxDtSec = 0.2f;
};

/// Wynik estymacji — wszystko w ukladzie motocykla (X przod, Y prawo, Z dol).
struct OrientationState {
    /// Przechyl [rad]. Dodatni = w PRAWO, ujemny = w LEWO.
    float rollRad = 0.0f;
    /// Pochylenie [rad]. Dodatnie = przod uniesiony.
    float pitchRad = 0.0f;

    /// Przyspieszenie wzdluzne [g] po kompensacji grawitacji i filtracji.
    /// Dodatnie = przyspieszanie, ujemne = hamowanie.
    float longitudinalG = 0.0f;
    /// Przyspieszenie boczne [g] po filtracji.
    float lateralG = 0.0f;

    /// Estymowany offset zyroskopu w ukladzie urzadzenia [rad/s].
    Vec3 gyroBiasRadS;

    bool stationary = false;
    /// Czy w ostatnim kroku zadzialala korekcja akcelerometrem.
    bool accelCorrectionActive = false;
    /// Czy w ostatnim kroku zadzialala korekcja z ustalonego zakretu.
    bool turnCorrectionActive = false;

    float rollDeg() const { return radToDeg(rollRad); }
    float pitchDeg() const { return radToDeg(pitchRad); }
};

class Orientation {
public:
    explicit Orientation(const OrientationConfig& config = {}) : config_(config) {}

    void setConfig(const OrientationConfig& config) { config_ = config; }
    const OrientationConfig& config() const { return config_; }

    void setMount(const MountCalibration& mount) { mount_ = mount; }
    const MountCalibration& mount() const { return mount_; }

    /// Przekazuje aktualna predkosc z GPS. Bez tego korekcja z ustalonego zakretu
    /// pozostaje nieaktywna, a przechyl opiera sie wylacznie na zyroskopie.
    void setSpeedHint(float speedMs, unsigned long timestampMs);

    /// Krok filtru. dtSec liczony miedzy kolejnymi probkami.
    void update(const ImuSample& sample, float dtSec);

    /// Zeruje estymate katow, zachowujac nauczony offset zyroskopu i kalibracje.
    void resetAngles();

    const OrientationState& state() const { return state_; }

    /// Przechyl wyliczony z zaleznosci dla ustalonego zakretu: tan(phi) = v*omega/g.
    /// Wystawione publicznie dla testow jednostkowych i narzedzia replay.
    static float leanFromCoordinatedTurn(float speedMs, float verticalTurnRateRadS);

private:
    void integrateGyro(float p, float q, float r, float dtSec);
    void applyAccelCorrection(const Vec3& accelBike, float gyroMagnitude, float yawRate, float dtSec);
    void applyTurnCorrection(float q, float r, unsigned long nowMs, float dtSec);
    void updateStationary(const Vec3& accelBike, float gyroMagnitude,
                          const Vec3& rawGyroDevice, unsigned long nowMs, float dtSec);
    void updateOutputAccel(const Vec3& accelBike, float dtSec);

    OrientationConfig config_{};
    MountCalibration mount_{};
    OrientationState state_{};

    bool initialized_ = false;
    unsigned long stationarySinceMs_ = 0;
    bool stationaryCandidate_ = false;

    float speedHintMs_ = 0.0f;
    unsigned long speedHintTimestampMs_ = 0;
    bool speedHintValid_ = false;
};

}  // namespace motion
