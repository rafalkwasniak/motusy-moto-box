// Motusy Moto Box — ekran sprzetowy (widok serwisowy pod KEY1 hold).
//
// Pokazuje, co faktycznie odpowiada na magistrali I2C oraz stan zasilania
// i pamieci. Nie jest czescia specyfikacji funkcjonalnej — powstal jako
// narzedzie do weryfikacji sprzetu, ale zostaje na stale: przy montazu na
// motocyklu pozwala jednym spojrzeniem sprawdzic, czy nic sie nie odlaczylo.

#pragma once

#include <M5Unified.h>

#include "ScreenBuffer.h"
#include "../hal/I2cScan.h"

namespace ui {

struct HardwareViewModel {
    const hal::I2cScan* scan = nullptr;
    float sampleRateHz = 0.0f;
    int batteryPercent = 0;
    int batteryMillivolts = 0;
    bool charging = false;
    /// Odfiltrowana obecnosc zasilania — to jej uzywa maszyna stanow.
    bool externalPower = false;
    /// Najdluzsza zaobserwowana przerwa miedzy impulsami ladowania [ms].
    uint32_t maxPulseGapMs = 0;
    /// Napiecie wejsciowe zmierzone przez PMIC [mV]. Jesli dziala, jest
    /// pewniejszym sygnalem obecnosci zasilania niz heurystyka tetna.
    int vbusMillivolts = 0;
    const char* stateName = "";
    /// Modul alarmowy wlaczony przez uzytkownika (§16).
    bool alarmEnabled = false;
    /// Silnik alarmu faktycznie czuwa. Rozjazd tych dwoch wartosci oznacza blad.
    bool alarmArmed = false;
    /// Ile czasu urzadzenie spedzilo w light sleep od zgaszenia ekranu [%].
    int sleepPercent = 0;
    uint32_t standbySeconds = 0;
    /// Sredni czas jednej iteracji petli miedzy snami [us].
    uint32_t awakeMicros = 0;
    bool bufferedDisplay = false;
    uint32_t freeHeapBytes = 0;
    /// Kolejka wysylki na motobix.motusy.top: ile przejazdow czeka i jaki
    /// numer dostal ostatni. Zajmuje miejsce po wolnej pamieci PSRAM, ktora
    /// od czasu jej wylaczenia (docs §4) zawsze pokazywala zero.
    uint32_t pendingUploads = 0;
    uint32_t lastRideSeq = 0;
};

class HardwareView {
public:
    void draw(ScreenBuffer& buffer, const HardwareViewModel& model);

private:
    static constexpr int kHeaderY = 8;
    static constexpr int kListTop = 26;
    static constexpr int kListStep = 13;
    static constexpr int kUnknownY = 92;
    static constexpr int kPowerY = 104;
    static constexpr int kAlarmY = 116;
    static constexpr int kMemoryY = 128;
};

}  // namespace ui
