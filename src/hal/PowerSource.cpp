#include "PowerSource.h"

#include <M5Unified.h>

namespace hal {

void PowerSource::begin(uint32_t nowMs) {
    rawCharging_ = M5.Power.isCharging();
    batteryMillivolts_ = M5.Power.getBatteryVoltage();
    vbusMillivolts_ = M5.Power.M5pm1.getVBUSVoltage();
    vbusUsable_ = vbusMillivolts_ >= config_.vbusPresentMv;

    // Przy starcie zakladamy stan zgodny z odczytem. Gdy urzadzenie wstaje
    // na baterii, `external_` bedzie false i maszyna stanow od razu wejdzie
    // w odliczanie zamiast udawac jazde.
    external_ = vbusUsable_ || rawCharging_;
    lastPulseMs_ = nowMs;
    lastPollMs_ = nowMs;
    maxPulseGapMs_ = 0;
    sawFirstPulse_ = rawCharging_;
}

bool PowerSource::update(uint32_t nowMs) {
    // Odpytywanie PMIC przez I2C w kazdej iteracji petli byloby marnotrawstwem
    // — miedzy odczytami oddajemy ostatni znany stan.
    if (nowMs - lastPollMs_ < config_.pollIntervalMs) return external_;
    lastPollMs_ = nowMs;

    rawCharging_ = M5.Power.isCharging();
    batteryMillivolts_ = M5.Power.getBatteryVoltage();
    vbusMillivolts_ = M5.Power.M5pm1.getVBUSVoltage();

    // Sygnal podstawowy: zmierzone napiecie wejsciowe. Gdy raz zobaczylismy
    // sensowna wartosc, wiemy ze rejestr dziala i ufamy mu juz na zawsze —
    // takze wtedy, gdy pokazuje zero, bo to po prostu brak zasilania.
    if (vbusMillivolts_ >= config_.vbusPresentMv) {
        vbusUsable_ = true;
        external_ = true;
        return external_;
    }
    if (vbusUsable_) {
        if (vbusMillivolts_ < config_.vbusAbsentMv) external_ = false;
        return external_;
    }

    // FALLBACK dla plytek, ktore nie oddaja napiecia wejsciowego.
    if (rawCharging_) {
        // Mierzymy przerwe miedzy impulsami tylko wtedy, gdy widzielismy juz
        // wczesniejszy impuls — inaczej pierwsza "przerwa" po odlaczeniu
        // i ponownym podlaczeniu zawyzalaby statystyke.
        if (sawFirstPulse_) {
            const uint32_t gap = nowMs - lastPulseMs_;
            if (gap > maxPulseGapMs_ && gap < config_.chargePulseHoldMs) {
                maxPulseGapMs_ = gap;
            }
        }
        lastPulseMs_ = nowMs;
        sawFirstPulse_ = true;
        external_ = true;
        return external_;
    }

    if (sawFirstPulse_ && nowMs - lastPulseMs_ < config_.chargePulseHoldMs) {
        // Przerwa w ladowaniu, ale zasilanie najprawdopodobniej nadal jest.
        return external_;
    }


    external_ = false;
    return external_;
}

}  // namespace hal
