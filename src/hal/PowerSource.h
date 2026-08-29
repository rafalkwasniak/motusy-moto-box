// Motusy Moto Box — wykrywanie zasilania zewnetrznego (architektura §5).
//
// SYGNAL PODSTAWOWY: napiecie wejsciowe z PMIC (`getVBUSVoltage`). Zmierzone
// na sprzecie (2026-08-29): 5,21 V z kablem, 0 V bez. Jednoznaczne
// i natychmiastowe — bez heurystyk i bez opoznienia.
//
// FALLBACK: `isCharging()` traktowane jak tetno. Uzywane tylko wtedy, gdy PMIC
// ani razu nie oddal sensownego napiecia wejsciowego (inna rewizja plytki).
// Sama funkcja `isCharging()` nie nadaje sie na sygnal podstawowy: nie mowi
// "czy jest prad", tylko "czy w tej chwili plynie prad ladowania", a przy
// baterii naladowanej do pelna **migocze** — ladowarka cyklicznie konczy
// i wznawia doladowywanie (zmierzone 2026-08-28).
//
// Napiecie BATERII celowo nie bierze udzialu w decyzji: swiezo odlaczona pelna
// bateria trzyma ~4,15 V przez dluzszy czas i udawalaby obecne zasilanie.
// Sluzy wylacznie do diagnostyki.

#pragma once

#include <cstdint>

namespace hal {

struct PowerSourceConfig {
    /// Progi napiecia wejsciowego z histereza [mV]. Zmierzone 5,21 V / 0 V,
    /// wiec progi leza z duzym zapasem po obu stronach.
    int vbusPresentMv = 4000;
    int vbusAbsentMv = 3000;

    /// FALLBACK: jak dlugo po ostatnim impulsie ladowania uznajemy zasilanie
    /// za obecne, gdy PMIC nie oddaje napiecia wejsciowego.
    uint32_t chargePulseHoldMs = 15000;

    /// Odstep miedzy odczytami z PMIC [ms]. Petla glowna kreci sie setki razy
    /// na sekunde — odpytywanie I2C w kazdej iteracji byloby marnotrawstwem.
    uint32_t pollIntervalMs = 200;
};

class PowerSource {
public:
    explicit PowerSource(const PowerSourceConfig& config = {}) : config_(config) {}

    void begin(uint32_t nowMs);

    /// Wywolywac cyklicznie. Zwraca odfiltrowana obecnosc zasilania.
    bool update(uint32_t nowMs);

    bool isExternal() const { return external_; }

    /// Surowy odczyt z ostatniej aktualizacji — do ekranu diagnostycznego.
    bool rawCharging() const { return rawCharging_; }
    int batteryMillivolts() const { return batteryMillivolts_; }
    int vbusMillivolts() const { return vbusMillivolts_; }

    /// Czy decyzja opiera sie na pomiarze napiecia (a nie na fallbacku).
    bool usingVbus() const { return vbusUsable_; }

    /// Najdluzsza zaobserwowana przerwa miedzy impulsami ladowania [ms].
    /// Sluzy do dobrania `chargePulseHoldMs` na konkretnym egzemplarzu:
    /// prog powinien byc wyraznie wiekszy od tej wartosci.
    uint32_t maxPulseGapMs() const { return maxPulseGapMs_; }

    /// Ile czasu minelo od ostatniego impulsu [ms].
    uint32_t msSincePulse(uint32_t nowMs) const { return nowMs - lastPulseMs_; }

private:
    PowerSourceConfig config_{};

    bool external_ = true;
    bool rawCharging_ = false;
    int batteryMillivolts_ = 0;
    int vbusMillivolts_ = 0;
    bool vbusUsable_ = false;
    uint32_t lastPollMs_ = 0;

    uint32_t lastPulseMs_ = 0;
    uint32_t maxPulseGapMs_ = 0;
    bool sawFirstPulse_ = false;
};

}  // namespace hal
