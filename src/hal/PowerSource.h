// Motusy Moto Box — wykrywanie zasilania zewnetrznego (architektura §5).
//
// PROBLEM. `M5.Power.isCharging()` nie odpowiada na pytanie "czy jest prad",
// tylko "czy w tej chwili plynie prad ladowania". Zmierzone na sprzecie
// (2026-08-28): przy baterii naladowanej do pelna i podlaczonym USB funkcja
// **migocze** — ladowarka cyklicznie konczy i wznawia doladowywanie.
//
// ROZWIAZANIE. Traktujemy ten sygnal jak tetno. Po podlaczeniu pulsuje prawda
// regularnie; po odlaczeniu nie pojawi sie nigdy. Zasilanie uznajemy wiec za
// obecne, jesli impuls byl w ciagu ostatnich kilkunastu sekund.
//
// Napiecie baterii CELOWO nie bierze udzialu w decyzji: swiezo odlaczona pelna
// bateria trzyma ~4,15 V przez dluzszy czas i udawalaby obecne zasilanie.
// Sluzy wylacznie do diagnostyki.

#pragma once

#include <cstdint>

namespace hal {

struct PowerSourceConfig {
    /// Jak dlugo po ostatnim impulsie ladowania uznajemy zasilanie za obecne.
    /// Musi byc wyraznie dluzsze niz najdluzsza przerwa miedzy impulsami przy
    /// pelnej baterii — patrz `maxPulseGapMs()`, ktore te przerwe mierzy.
    /// Wartosc produkcyjna pochodzi z cfg::kChargePulseHoldMs.
    uint32_t chargePulseHoldMs = 15000;
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

    uint32_t lastPulseMs_ = 0;
    uint32_t maxPulseGapMs_ = 0;
    bool sawFirstPulse_ = false;
};

}  // namespace hal
