// Motusy Moto Box — modul GPS na Grove Port A (M5Stack Unit GPS, AT6668).
//
// Rola tej klasy jest taka sama jak ImuSource: zamiana sprzetu na neutralna
// strukture dla warstwy algorytmicznej (`motion::SpeedSample`). Parsowanie
// NMEA siedzi w `lib/gps`, wiec kompiluje sie i testuje bez sprzetu.
//
// DOBOR USTAWIEN UART-U JEST AUTOMATYCZNY. Dokumentacja podaje 9600 baud,
// ale sztuki bywaja ustawione inaczej, a kolejnosc zyl w kablu Grove decyduje
// o tym, ktory pin jest odbiorczy. Zamiast zakladac — probujemy po kolei
// czterech kombinacji (dwa piny x dwie predkosci) i zostajemy przy tej, na
// ktorej przyszlo zdanie z poprawna suma kontrolna. To rozstrzyga na sprzecie
// dwie niewiadome, ktorych z dokumentacji rozstrzygnac sie nie da.
//
// ZASILANIE: modul bierze ~32 mA z wyjscia 5 V na Grove. Architektura §2.5
// wymaga, zeby bylo odcinane poza jazda — inaczej bateria 250 mAh znika
// w ciagu nocy. Stad `setPower()` sterowane stanem urzadzenia, a nie wlaczone
// raz przy starcie.

#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "NmeaParser.h"
#include "RideMetrics.h"

namespace hal {

struct GpsSourceConfig {
    /// Grove Port A na StickS3: SCL=G10, SDA=G9 (tabela pinow M5Unified).
    /// Moduly M5Stack wystawiaja swoje TX na linii SCL, wiec to jest wariant
    /// pierwszy — drugi (zamieniony) i tak zostanie sprawdzony.
    int rxPin = 10;
    int txPin = 9;
    /// Ile czekamy na pierwsze poprawne zdanie, zanim sprobujemy inaczej.
    /// Modul nadaje raz na sekunde, wiec dwie sekundy to dwie szanse.
    uint32_t probeMs = 2000;
    /// Po jakim czasie bez swiezego fixa uznajemy, ze predkosci nie mamy.
    /// Tunel, wiadukt, garaz — §2.5 wymaga degradacji, nie zamarcia.
    uint32_t fixMaxAgeMs = 5000;
    gps::NmeaQuality quality{};
};

/// Podsumowanie jednej nieudanej proby. Odpowiada na pytanie, ktore przy
/// milczacym module jest jedynym istotnym: czy z tego pinu w ogole cokolwiek
/// leci. Bajty bez zdan = zla predkosc transmisji; zero bajtow = zly pin,
/// brak zasilania albo odlaczony kabel.
struct GpsProbeReport {
    uint32_t baud = 0;
    int rxPin = 0;
    uint32_t bytes = 0;
    uint32_t validSentences = 0;
    uint32_t rejectedSentences = 0;
};

class GpsSource {
public:
    explicit GpsSource(const GpsSourceConfig& config = {}) : config_(config) {}

    /// Otwiera UART na pierwszej kombinacji ustawien. Nie wlacza zasilania —
    /// o tym decyduje stan urzadzenia przez `setPower()`.
    void begin(uint32_t nowMs);

    /// Wyjscie 5 V na Grove. Wywolanie z ta sama wartoscia nic nie kosztuje.
    void setPower(bool on, uint32_t nowMs);
    bool isPowered() const { return powered_; }

    /// Czyta wszystko, co przyszlo z UART-u.
    /// @return true gdy domknelo sie zdanie RMC, czyli jest nowa probka predkosci.
    bool update(uint32_t nowMs);

    /// Probka dla `RideMetrics` i filtru orientacji. `valid` jest tu prawda
    /// tylko wtedy, gdy fix jest potwierdzony ORAZ swiezy.
    motion::SpeedSample speed(uint32_t nowMs) const;

    /// Czy mamy aktualny fix — sterowanie napisem "---" na ekranie wynikow.
    bool hasFix(uint32_t nowMs) const;

    /// Czy z modulu w ogole cokolwiek przychodzi (dobrana predkosc transmisji).
    bool isReceiving() const { return locked_; }

    /// Czas UTC jako uniksowy znacznik [s]; 0 gdy modul jeszcze go nie podal.
    /// Miedzy zdaniami (i po odcieciu zasilania modulu) doliczany jest uplyw
    /// millis() — zegar urzadzenia chodzi dalej, tylko przestaje byc
    /// korygowany. To jedyne zrodlo daty w systemie: RTC na plytce nie ma.
    uint32_t unixTime(uint32_t nowMs) const;
    bool hasTime() const { return lastEpoch_ != 0; }

    uint32_t baud() const { return baud_; }
    int rxPin() const { return rxPin_; }
    uint8_t satellites() const { return parser_.fix().satellites; }
    float hdop() const { return parser_.fix().hdop; }
    /// Ostatnia odebrana predkosc [km/h], niezaleznie od swiezosci — do diagnostyki.
    float lastSpeedKmh() const { return parser_.fix().speedKmh; }
    uint32_t validSentences() const { return parser_.validSentences(); }
    uint32_t rejectedSentences() const { return parser_.rejectedSentences(); }

    /// Odbiera podsumowanie ostatniej nieudanej proby (jeden raz).
    /// @return false gdy nie ma nic nowego do zaraportowania.
    bool takeProbeReport(GpsProbeReport& out);

    /// Podglad surowych zdan na wskazanym strumieniu. `nullptr` wylacza.
    /// Jedyne narzedzie diagnostyczne, ktore odpowiada na pytanie "czy modul
    /// cokolwiek mowi", gdy parser milczy.
    void setEcho(Stream* echo) { echo_ = echo; }
    bool isEchoing() const { return echo_ != nullptr; }

private:
    void applyProbe(uint32_t nowMs);
    void advanceProbe(uint32_t nowMs);

    GpsSourceConfig config_{};
    gps::NmeaParser parser_{};
    HardwareSerial serial_{1};
    Stream* echo_ = nullptr;

    bool started_ = false;
    bool powered_ = false;
    /// Ustawienia UART-u potwierdzone poprawnym zdaniem — koniec prob.
    bool locked_ = false;
    /// Numer sprawdzanej kombinacji: predkosc transmisji razy kolejnosc pinow.
    uint8_t probeIndex_ = 0;
    uint32_t probeStartedMs_ = 0;
    /// Ile bajtow przyszlo w biezacej probie — niezaleznie od tego, czy dalo
    /// sie z nich cokolwiek zlozyc.
    uint32_t probeBytes_ = 0;

    GpsProbeReport report_{};
    bool reportPending_ = false;

    uint32_t baud_ = 0;
    int rxPin_ = 0;

    uint32_t lastSentenceMs_ = 0;
    uint32_t lastFixMs_ = 0;
    /// Ostatni znacznik czasu z modulu i chwila, w ktorej przyszedl.
    uint32_t lastEpoch_ = 0;
    uint32_t lastEpochMs_ = 0;
    bool everFixed_ = false;
};

}  // namespace hal
