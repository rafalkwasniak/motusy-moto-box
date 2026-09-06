// Motusy Moto Box — caly tor pomiaru halasu w jednej klasie.
//
//     probki znormalizowane do <-1, 1>  (48 kHz)
//           |
//           +--> wykrycie przesterowania    miernik, ktory cicho obcina, KLAMIE
//           +--> AWeighting                 filtr A, IEC 61672
//           +--> TimeWeighting Fast         tau = 125 ms, usrednianie NA MOCY
//           +--> co 10 ms --> SustainedLevel   percentyl w oknie 5 s
//           +--> maksimum + predkosc w chwili rekordu
//
// CEL JEST WZGLEDNY, NIE BEZWZGLEDNY. Urzadzenie siedzi w zadupku motocykla:
// zaslonete, we wnece, blisko wydechu. Pomiar bezwzgledny w dB(A) bylby w tych
// warunkach fikcja i nie probujemy go udawac. Chcemy liczby POWTARZALNEJ
// WZGLEDEM SAMEJ SIEBIE przez sezon — patrz docs/pomiar-halasu.md §1.
//
// Wynika z tego rozklad wymagan inny, niz zwykle: bledy systematyczne
// (rezonans wneki, zaslonieciecie) sa ZA DARMO, bo identyczne w maju
// i we wrzesniu. Za to niezmiennosc wzmocnienia jest swieta — dlatego ALC
// w ES8311 musi byc wylaczone, a kazda zmiana gainu podnosi `noise_cal`.
//
// DWIE RZECZY, KTORYCH TA KLASA PILNUJE, A KTORE LATWO PRZEOCZYC:
//
//   1. FILTRY PRACUJA ZAWSZE, MAKSIMUM ZBIERAMY WARUNKOWO. Zerowanie filtru A
//      albo okna miedzy fragmentami jazdy zafalszowaloby narastanie. Karmimy
//      wiec bez przerwy, a bramka predkosci steruje wylacznie tym, czy wynik
//      moze ustanowic rekord.
//
//      OKNO MUSI W CALOSCI POCHODZIC Z CZASU PO OTWARCIU BRAMKI. Samo
//      "okno pelne" nie wystarcza, bo okno jest przesuwne: zaraz po otwarciu
//      bramki niesie jeszcze piec sekund dzwieku sprzed niej. Znaczyloby to,
//      ze blip gazem na czerwonym swietle ustanawia rekord przejazdu —
//      czyli dokladnie to, czemu bramka miala zapobiec.
//
//      Kosztuje to pierwsze 5 s po KAZDYM otwarciu bramki, takze po postoju
//      na swiatlach. Koszt przyjety swiadomie: metryka ma opisywac jazde,
//      a nie postoj, a przy 5-sekundowym oknie krotszej drogi nie ma.
//
//   2. REKORD NIESIE WLASNA DIAGNOZE. Wartosc nie trafia na ekran (decyzja
//      uzytkownika — idzie wylacznie przez API), wiec nie ma jak zobaczyc,
//      ze pomiar padl. A martwy mikrofon nie daje bledu, tylko podloge szumu
//      wygladajaca jak cicha jazda. Stad liczniki przesterowan i zgubionych
//      blokow: bez nich awaria jest nieodrozninalna od ciszy.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstdint>

#include "AWeighting.h"
#include "SustainedLevel.h"
#include "TimeWeighting.h"

namespace noise {

struct NoiseMeterConfig {
    float sampleRateHz = 48000.0f;
    /// Fast wedlug IEC 61672. Patrz TimeWeighting.h.
    float fastTauS = 0.125f;
    /// Co ile ms poziom trafia do okna. 10 ms daje 500 probek na 5 s.
    float stepMs = 10.0f;
    /// Ile halas ma trwac, zeby sie liczyl. Piec sekund to decyzja
    /// uzytkownika: "na motocyklu 5 s to nie jest dlugo".
    float windowSec = 5.0f;
    /// Przez jaki procent okna poziom ma byc utrzymany.
    float tolerancePercent = 90.0f;
    /// dB(A) = dBFS + calibrationDb.
    ///
    /// UWAGA NA ZNAK. Zeszlismy ze wzmocnieniem cyfrowym o 12 dB dla zapasu
    /// przed przesterowaniem, wiec to samo zrodlo daje teraz NIZSZY dBFS —
    /// a zatem K musi ROSNAC: 121,6 + 12 = 133,6. Odwrocenie tego rozumowania
    /// daje liczby zanizone o 24 dB, wygladajace przy tym calkowicie
    /// wiarygodnie. Patrz docs/pomiar-halasu.md §8.
    float calibrationDb = 133.6f;
};

class NoiseMeter {
public:
    /// Poziom, od ktorego uznajemy probke za przesterowana. Normalizacja to
    /// x = s / 32768, wiec pelna skala int16 to 0,99997.
    static constexpr float kClipLevel = 0.999f;

    static constexpr float kNoData = SustainedLevel::kNoData;

    explicit NoiseMeter(const NoiseMeterConfig& config = {}) { configure(config); }

    void configure(const NoiseMeterConfig& config);

    /// Nowy przejazd: zeruje wynik, liczniki i caly stan toru.
    void reset();

    /// Bramka predkosci (motion::SpeedGate). Steruje WYLACZNIE zbieraniem
    /// maksimum — filtry pracuja niezaleznie od niej.
    void setRecording(bool recording) { recording_ = recording; }

    /// Biezaca predkosc z GPS. Zapamietywana w chwili ustanowienia rekordu,
    /// zeby dalo sie potem odroznic wydech od wiatru: jesli `noise_at_speed`
    /// prawie zawsze rowna sie predkosci maksymalnej przejazdu, mierzymy
    /// powietrze. Patrz docs/pomiar-halasu.md §6.
    void setSpeedKmh(float kmh) { speedKmh_ = kmh; }

    /// Jedna probka znormalizowana do <-1, 1>.
    void addSample(float x);

    /// Blok(i) zgubione przez przepelnienie DMA. Zgubionych probek nie da sie
    /// odtworzyc, ale da sie o nich powiedziec — a to wystarczy, zeby nie
    /// wziac niepelnego pomiaru za cichy przejazd.
    void addDropped(uint32_t blocks) { dropped_ += blocks; }

    /// Najwyzszy utrzymany poziom przejazdu w dB(A). kNoData = nic nie
    /// zmierzono (bramka nigdy sie nie otworzyla albo okno sie nie wypelnilo).
    float maxNoiseDb() const;

    /// Predkosc w chwili ustanowienia rekordu [km/h]. Bez sensu, gdy
    /// maxNoiseDb() == kNoData.
    float maxNoiseSpeedKmh() const { return maxSpeedKmh_; }

    /// Poziom utrzymany w biezacym oknie, w dB(A). Do diagnostyki na porcie
    /// USB — bez ekranu to jedyne okno na pomiar.
    float currentDb() const;

    /// Poziom chwilowy po wazeniu A i Fast, w dB(A). Reaguje od razu, wiec
    /// przy diagnostyce widac po nim, ze mikrofon w ogole zyje.
    float instantDb() const;

    uint32_t clippedCount() const { return clipped_; }
    uint32_t droppedCount() const { return dropped_; }

    /// Czy okno zdazylo sie wypelnic od ostatniego reset().
    bool ready() const { return sustained_.ready(); }

private:
    NoiseMeterConfig config_{};
    AWeighting weighting_{};
    TimeWeighting fast_{};
    SustainedLevel sustained_{};

    /// Co ile probek poziom trafia do okna.
    uint32_t stepDivider_ = 1;
    uint32_t stepCounter_ = 0;

    bool recording_ = false;
    /// Ile krokow okna uzbieralo sie od OTWARCIA bramki. Maksimum zbieramy
    /// dopiero, gdy jest ich tyle, ile miesci okno.
    uint32_t armedSteps_ = 0;
    float speedKmh_ = 0.0f;

    float maxDb_ = kNoData;
    float maxSpeedKmh_ = 0.0f;

    uint32_t clipped_ = 0;
    uint32_t dropped_ = 0;
};

}  // namespace noise
