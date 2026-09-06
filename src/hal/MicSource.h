// Motusy Moto Box — mikrofon na kodeku ES8311 (I2S, 48 kHz).
//
// Rola ta sama, co GpsSource i ImuSource: zamiana sprzetu na neutralna
// strukture dla warstwy algorytmicznej. Caly pomiar siedzi w `lib/noise`,
// wiec kompiluje sie i testuje bez sprzetu — ta klasa tylko go karmi.
//
// M5UNIFIED ZNA TA PLYTKE, ALE JEGO USTAWIENIA SA POD MOWE, NIE POD POMIAR.
// Trzy wartosci domyslne trzeba zdjac, bo kazda z nich po cichu zafalszowuje
// wynik, a zadna nie zglasza sie bledem:
//
//   magnification = 16   mnoznik cyfrowy, czyli +24 dB, o ktore nikt nie prosil.
//                        Zabiera caly zapas przed przesterowaniem.
//   over_sampling = 2    usrednianie dwoch probek. To jest DODATKOWY FILTR
//                        dolnoprzepustowy w torze, ktory ma miec dokladnie
//                        okreslona charakterystyke (wazenie A). Nie do przyjecia.
//   0x17 = 0xFF          ADC_VOLUME na maksimum, czyli kolejne +32 dB.
//                        Dokladnie ta pulapka, przed ktora ostrzega
//                        docs/noice.md §2: "nie zostawiaj 0xFF z M5Unified".
//
// Razem to +56 dB niezamowionego wzmocnienia. Ustawienia wychodzace sa
// w `MicSourceConfig` i KAZDA ich zmiana wymaga podniesienia
// `cfg::kNoiseCalibrationVersion` — inaczej sezon rozjedzie sie po cichu.
//
// WLASNE ZADANIE, NIE PETLA GLOWNA. Przepelnienie bufora I2S nie zglasza sie
// bledem, tylko gubi probki — a pomiar oparty na niepelnych danych wyglada
// dokladnie tak samo, jak cicha jazda. Zadanie siedzi na rdzeniu 0 (petla
// Arduino zyje na 1), a do petli oddaje gotowa migawke.
//
// ZASILANIE I KODEK WSPOLDZIELONY Z SYRENA. ES8311 obsluguje mikrofon
// i glosnik, ale nie naraz. Rozdzial jest naturalny i te stany sie nie
// przecinaja: mikrofon wylacznie w jezdzie, syrena wylacznie po uzbrojeniu
// alarmu. `end()` musi byc wolane przed uspieniem urzadzenia.

#pragma once

#include <Arduino.h>

#include "NoiseMeter.h"

namespace hal {

struct MicSourceConfig {
    /// 48 kHz — patrz lib/noise/AWeighting.h. Nizej transformacja biliniowa
    /// deformuje charakterystyke na gorze pasma, a motocykl ma tam sporo
    /// energii (ssanie, lancuch, wydech).
    uint32_t sampleRateHz = 48000;

    /// ES8311 ADC_VOLUME (rejestr 0x17). 0xBF to 0 dB, krok 0,5 dB.
    /// 0xE7 = +20 dB, czyli 12 dB PONIZEJ tego, co ustawia M5Unified.
    /// Zejscie kupuje zapas przed przesterowaniem: mikrofon siedzi we wnece
    /// blisko wydechu i nie ma tam nic ciszej niz 60 dB, wiec podlogi nie
    /// zalujemy, a sufit jest realnie osiagalny.
    uint8_t adcVolume = 0xE7;

    /// Ktory kanal I2S niesie mikrofon. Wartosc WYJSCIOWA, nie pewnik —
    /// begin() mierzy energie obu i mowi, ktory to naprawde.
    bool useLeftChannel = false;

    /// Ile probek na blok. 512 przy 48 kHz to 10,7 ms — na tyle malo,
    /// zeby zadanie oddawalo procesor czesto, i na tyle duzo, zeby narzut
    /// na blok byl nieistotny.
    static constexpr size_t kBlockSamples = 512;
};

/// Wszystko, co petla glowna moze chciec wiedziec o pomiarze. Kopiowane
/// w calosci pod blokada, zeby maksimum i predkosc przy nim nigdy nie
/// rozjechaly sie miedzy soba.
struct MicSnapshot {
    float maxNoiseDb = noise::NoiseMeter::kNoData;
    float maxNoiseSpeedKmh = 0.0f;
    float currentDb = noise::NoiseMeter::kNoData;
    float instantDb = noise::NoiseMeter::kNoData;
    uint32_t clipped = 0;
    uint32_t dropped = 0;
    bool ready = false;
};

class MicSource {
public:
    explicit MicSource(const MicSourceConfig& config = {}) : config_(config) {}

    /// Uruchamia kodek, dobiera kanal i startuje zadanie pomiarowe.
    /// @return false gdy plytka nie ma skonfigurowanego mikrofonu.
    bool begin(const noise::NoiseMeterConfig& meterConfig);

    /// Zatrzymuje zadanie i oddaje kodek. Wolac przed uspieniem i przed
    /// wlaczeniem syreny.
    void end();

    bool isRunning() const { return taskHandle_ != nullptr; }

    /// Bramka predkosci — steruje wylacznie zbieraniem maksimum.
    void setRecording(bool recording);

    /// Biezaca predkosc, zapamietywana w chwili ustanowienia rekordu.
    void setSpeedKmh(float kmh);

    /// Nowy przejazd: zeruje wynik i liczniki, nie rusza kodeka.
    void resetRide();

    MicSnapshot snapshot() const;

    // ── Diagnostyka (komenda HALAS na porcie USB) ──────────────────────────
    // Bez ekranu to jedyne okno na pomiar, wiec te liczby nie sa ozdoba.

    /// Energia obu kanalow zmierzona przy starcie, w dBFS. Jesli oba sa
    /// martwe, czytamy pin glosnika zamiast mikrofonu.
    float probeLeftDb() const { return probeLeftDb_; }
    float probeRightDb() const { return probeRightDb_; }
    bool usesLeftChannel() const { return config_.useLeftChannel; }
    uint32_t sampleRateHz() const { return config_.sampleRateHz; }
    uint8_t adcVolume() const { return config_.adcVolume; }
    /// Ile blokow przerobiono od startu — rosnie, dopoki mikrofon zyje.
    uint32_t blocks() const;

private:
    static void taskEntry(void* arg);
    void taskLoop();
    void processBlock(const int16_t* samples, size_t count);
    void probeChannels();
    void applyCodecRegisters();

    static constexpr size_t kBuffers = 3;

    MicSourceConfig config_{};
    noise::NoiseMeter meter_{};

    int16_t* buffers_[kBuffers] = {nullptr, nullptr, nullptr};
    TaskHandle_t taskHandle_ = nullptr;
    volatile bool stopRequested_ = false;

    /// Wejscia z petli glownej i wyjscia do niej. Sekcje krytyczne sa
    /// kilkuinstrukcyjne, wiec spinlock jest tansszy niz mutex.
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    bool pendingRecording_ = false;
    float pendingSpeedKmh_ = 0.0f;
    bool pendingReset_ = false;
    MicSnapshot published_{};
    uint32_t blocks_ = 0;

    float probeLeftDb_ = noise::NoiseMeter::kNoData;
    float probeRightDb_ = noise::NoiseMeter::kNoData;

    /// Do wykrywania przerw w strumieniu. Porownujemy odstep miedzy blokami
    /// z jego czasem trwania — dryf zegara nie ma znaczenia, bo liczymy
    /// tylko grube przerwy.
    uint32_t lastBlockUs_ = 0;
    uint32_t blockUs_ = 0;
};

}  // namespace hal
