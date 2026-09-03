// Motusy Moto Box — bramka predkosci (architektura §16).
//
// PROBLEM: przechyl motocykla przy 2 km/h nie jest rekordem przechylu. Dojazd
// do skrzyzowania, utrata rownowagi przy 3 km/h, postawienie na bocznej nozce
// przy wlaczonym zaplonie, prowadzenie motocykla obok siebie — kazde z tych
// zdarzen ustanawialo rekord calej sesji. Skutek jest gorszy niz pojedyncza
// zla liczba: rekord przypadkowy ZAWSZE wygrywa z prawdziwym, bo jest wiekszy.
//
// ROZWIAZANIE: pomiary zapisujemy dopiero powyzej progu predkosci.
//
// Trzy rzeczy, ktore ta klasa musi umiec, a ktorych naiwne "jesli v > 5" nie umie:
//
//   1. HISTEREZA. Bez niej rejestracja migocze przy 4,9 / 5,1 km/h.
//   2. WYBIEG. Awaryjne hamowanie konczy sie na zerze, a jego ostatnia faza
//      bywa najostrzejsza — bez wybiegu bramka odcielaby dokladnie to, co
//      najciekawsze.
//   3. DEGRADACJA. Utrata fixu nie moze oznaczac "nie rejestruje nic". Po
//      uplywie czasu podtrzymania wracamy do reguly sprzed GPS-a, opartej na
//      `stationary` z IMU. Awaria modulu ma cofnac urzadzenie do stanu
//      sprzed GPS, a nie wylaczyc pomiary.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstdint>

namespace motion {

struct SpeedGateConfig {
    /// Ponizej tego progu motocykl jest manewrowany, a nie prowadzony.
    float enterKmh = 5.0f;
    /// Prog wyjscia nizszy niz wejscia — inaczej bramka migocze.
    float exitKmh = 3.0f;
    /// Ile jeszcze rejestrujemy po spadku ponizej progu wyjscia.
    uint32_t coastMs = 2000;
    /// Jak dlugo ostatnia znana predkosc pozostaje podstawa decyzji.
    /// Tunel, wiadukt, gesta zabudowa — po tym czasie zostaje sam IMU.
    uint32_t fixHoldMs = 15000;
};

class SpeedGate {
public:
    explicit SpeedGate(const SpeedGateConfig& config = {}) : config_(config) {}

    void setConfig(const SpeedGateConfig& config) { config_ = config; }

    /// Nowa probka predkosci. Podawac WYLACZNIE probki z potwierdzonym fixem —
    /// samo odebranie zdania NMEA to za malo (patrz gps::NmeaParser).
    void updateSpeed(float kmh, uint32_t nowMs);

    /// Czy w tej chwili wolno zapisywac pomiary.
    /// @param imuStationary bezruch wykryty przez IMU — zrodlo zapasowe,
    ///        uzywane dopiero gdy predkosci brakuje dluzej niz `fixHoldMs`.
    bool isRecording(bool imuStationary, uint32_t nowMs) const;

    /// Czy decyzja opiera sie na predkosci z GPS, czy juz na samym IMU.
    /// Do diagnostyki — po tym widac, czy bramka w ogole dziala.
    bool hasFreshSpeed(uint32_t nowMs) const;

    /// Nowa sesja jazdy.
    void reset();

private:
    /// Czy wybieg po hamowaniu juz sie skonczyl.
    bool coastExpired(uint32_t nowMs) const;

    SpeedGateConfig config_{};

    /// Bramka otwarta: predkosc przekroczyla prog wejscia i jeszcze nie
    /// zamknelismy jej po wybiegu.
    bool open_ = false;
    /// Predkosc jest ponizej progu wyjscia od `belowSinceMs_`.
    bool below_ = false;
    uint32_t belowSinceMs_ = 0;

    bool haveSpeed_ = false;
    uint32_t lastSpeedMs_ = 0;
};

}  // namespace motion
