// Motusy Moto Box — maszyna stanow urzadzenia (§6, §17-§19, §21, §26).
//
// Jedynym sygnalem sterujacym jest obecnosc zasilania zewnetrznego: to ono
// decyduje o rozpoczeciu nowej sesji jazdy, o zgaszeniu ekranu i o uzbrojeniu
// alarmu. Dlatego cala logika przejsc siedzi tutaj, w czystym C++, i da sie
// ja przetestowac bez wypinania kabla.
//
//        ┌──────────────────────────────────────────────┐
//        ▼                                              │
//   ┌─────────┐   zanik zasilania   ┌──────────┐        │
//   │  LOTKA  │ ──────────────────► │ CZUWANIE │        │
//   │ (jazda) │ ◄────────────────── │  0-2 min │        │
//   └─────────┘   zasilanie wraca   └──────────┘        │
//        ▲        • nowa sesja           │ uplynely     │
//        │        • LOTKA = 0            │ 2 minuty     │
//        │        • alarm rozbrojony     ▼              │
//        │                          ┌──────────┐        │
//        └──────────────────────────│ USPIENIE │        │
//                zasilanie wraca    │ ekran OFF│        │
//                                   └──────────┘        │
//                                        │ ruch         │
//                                        ▼              │
//                                   ┌──────────┐        │
//                                   │  ALARM   │────────┘
//                                   └──────────┘  zasilanie wraca
//
// ODPORNOSC NA ZAKLOCENIA — oba bledy sa kosztowne i wymagaja potwierdzenia
// czasowego, ale z roznych powodow:
//
//   falszywy "zanik"  -> po 2 minutach alarm zawyje w trakcie jazdy.
//                        Rozruch motocykla zapada napiecie na ulamek sekundy,
//                        wiec zanik potwierdzamy dluzej.
//   falszywy "powrot" -> alarm sam sie rozbroi i zlodziej wygrywa.

#pragma once

#include <cstdint>

namespace state {

enum class DeviceState {
    /// Jazda: jest zasilanie z motocykla, zbieramy pomiary.
    Riding,
    /// Okres po zgasnieciu stacyjki. Ekran swieci, wyniki widoczne,
    /// alarm jeszcze nieuzbrojony (§18).
    Cooldown,
    /// Ekran zgaszony, alarm wylaczony — samo oszczedzanie baterii.
    Idle,
    /// Ekran zgaszony, alarm pilnuje ruchu (§19).
    Armed,
    /// Wykryto ruch, trwa sygnalizacja (§20).
    Triggered,
};

/// Zdarzenia, na ktore warstwa aplikacji musi zareagowac. Zwracane raz,
/// w momencie przejscia — nie w kazdej iteracji.
enum class DeviceEvent {
    None,
    /// Zaczyna sie nowa sesja: wyzerowac LOTKE, rozbroic alarm (§21, §26).
    RideStarted,
    /// Zniklo zasilanie: zapisac wyniki do pamieci, wlaczyc odliczanie (§17).
    PowerLost,
    /// Uplynely 2 minuty: zgasic ekran, uzbroic alarm jesli wlaczony.
    ScreenOff,
    /// Wykryto ruch przy uzbrojonym alarmie.
    MotionDetected,
    /// Sygnalizacja zakonczona, wracamy do czuwania.
    AlarmCleared,
};

struct DeviceStateConfig {
    /// §18 — ile czekamy od zaniku zasilania do zgaszenia ekranu.
    uint32_t armingDelayMs = 120000;

    /// Jak dlugo brak zasilania musi sie utrzymac, zanim uznamy zgasniecie
    /// stacyjki. Dluzej niz powrot, bo rozruch silnika zapada napiecie.
    uint32_t powerLossConfirmMs = 5000;

    /// Jak dlugo zasilanie musi byc obecne, zanim uznamy wlaczenie stacyjki.
    uint32_t powerReturnConfirmMs = 1500;
};

class DeviceStateMachine {
public:
    explicit DeviceStateMachine(const DeviceStateConfig& config = {}) : config_(config) {}

    const DeviceStateConfig& config() const { return config_; }

    /// Krok maszyny.
    /// @param externalPower  odfiltrowany sygnal obecnosci zasilania
    /// @param alarmEnabled   stan modulu alarmowego (§16)
    /// @param motionDetected czy detektor ruchu zglosil naruszenie
    DeviceEvent update(bool externalPower, bool alarmEnabled, bool motionDetected,
                       uint32_t nowMs);

    /// Ustawia stan poczatkowy po starcie urzadzenia, bez generowania zdarzen.
    void begin(bool externalPower, uint32_t nowMs);

    DeviceState state() const { return state_; }

    /// Ile zostalo do zgaszenia ekranu [ms]. Zero poza stanem Cooldown.
    uint32_t msUntilScreenOff(uint32_t nowMs) const;

    bool screenShouldBeOn() const {
        return state_ == DeviceState::Riding || state_ == DeviceState::Cooldown;
    }

    /// Czy urzadzenie moze spac miedzy probkami.
    bool maySleep() const {
        return state_ == DeviceState::Idle || state_ == DeviceState::Armed;
    }

    /// Wycisza trwajaca sygnalizacje i wraca do czuwania — reakcja na przycisk.
    void silence(uint32_t nowMs);

    /// Automatyczny powrot z Triggered do Armed po zakonczeniu sygnalizacji —
    /// alarm ma pilnowac dalej, nie zostac na zawsze w stanie "RUCH!".
    void rearm();

private:
    /// Filtruje sygnal zasilania. Zwraca stan potwierdzony czasowo.
    bool debouncePower(bool externalPower, uint32_t nowMs);

    DeviceStateConfig config_{};
    DeviceState state_ = DeviceState::Riding;

    bool confirmedPower_ = true;
    bool pendingPower_ = true;
    uint32_t pendingSinceMs_ = 0;

    uint32_t powerLostAtMs_ = 0;
};

const char* stateName(DeviceState state);

}  // namespace state
