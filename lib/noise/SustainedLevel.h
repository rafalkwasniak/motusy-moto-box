// Motusy Moto Box — poziom UTRZYMANY w ruchomym oknie.
//
// Odpowiada na pytanie "jak glosno bylo przez co najmniej piec sekund?",
// a nie "jak glosno bylo w najglosniejszej chwili?".
//
// PO CO. Zwykle maksimum (LAFmax) ustanowiloby rekord sezonu pierwszego dnia:
// kamien w owiewke, trzask kasku o bak, klasniecie przy zmianie biegu — kazde
// z nich bije wydech o kilkanascie decybeli i trwa 50 ms. Kolumna "najglosniejszy
// przejazd" pokazywalaby wtedy "przejazd, w trakcie ktorego cos stuknelo".
// To ta sama patologia, co przy bramce predkosci (architektura §16.1): rekord
// przypadkowy ZAWSZE wygrywa z prawdziwym, bo jest wiekszy.
//
// USREDNIANIE PO 5 s TEZ NIE DZIALA. Pik 120 dB trwajacy 100 ms na tle 50 dB
// daje LAeq,5s = 103 dB — stracil 17 dB i nadal wygrywa z kazdym realnym
// warkotem. Energia nie znika od tego, ze podzielisz ja przez dluzszy czas.
//
// CO DZIALA: minimum ruchome (erozja). Przesuwasz po przebiegu okno 5 s,
// w kazdym polozeniu bierzesz minimum, a na koniec maksimum z tych minimow.
// Pik ZNIKA CALKOWICIE, a nie "zostaje oslabiony". Warunek jest jakosciowy:
// albo halas trwal 5 s, albo nie.
//
// POPRAWKA KONIECZNA: TOLERANCJA. Czyste minimum jest zbyt surowe — jedna
// niska probka zabija cale okno, a mamy dwa mechanizmy, ktore takie zapady
// produkuja: motocykl faluje na obrotach, a sam filtr Fast potrzebuje ~0,28 s
// na narastanie. Dzwiek trwajacy DOKLADNIE 5,0 s dalby pelny poziom tylko
// przez ~4,7 s i czyste minimum by go odrzucilo — czyli metryka ">= 5 s"
// nie wykrylaby zdarzenia trwajacego 5 s.
//
// Zamiast minimum bierzemy wiec niski percentyl okna: "przez co najmniej 90 %
// czasu" zamiast "przez caly czas". Przy p = 90 % tolerowany zapad to 0,5 s,
// czyli narastanie miesci sie z zapasem.
//
// CZEGO TA KLASA CELOWO NIE ROBI: nie pamieta maksimum. W dokumencie zrodlowym
// (docs/noice.md §5.5) maksimum siedzialo tutaj, ale u nas jest ono decyzja
// POLITYCZNA, a nie pomiarowa — zbieramy je tylko przy otwartej bramce
// predkosci, zeby krecenie gazem na postoju nie ustanawialo rekordu przejazdu.
// Okno karmimy zawsze, maksimum uzbraja NoiseMeter. Klasa robi jedna rzecz:
// percentyl w ruchomym oknie.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

namespace noise {

class SustainedLevel {
public:
    /// Najwieksze obslugiwane okno: 5 s przy kroku 10 ms.
    static constexpr size_t kMaxSamples = 500;

    /// Koszyki histogramu: 0,5 dB, od -140 do +10 dBFS.
    static constexpr float kMinDb = -140.0f;
    static constexpr float kBinWidthDb = 0.5f;
    static constexpr size_t kBins = 300;

    /// Zwracane, dopoki okno sie nie wypelnilo — niepelne okno nic nie orzeka.
    static constexpr float kNoData = -1000.0f;

    explicit SustainedLevel(float windowSec = 5.0f, float stepMs = 10.0f,
                            float tolerancePercent = 90.0f) {
        configure(windowSec, stepMs, tolerancePercent);
    }

    /// @param windowSec        ile sekund halas ma trwac
    /// @param stepMs           co ile ms przychodzi odczyt poziomu
    /// @param tolerancePercent przez jaki % okna poziom ma byc utrzymany
    ///                         (100 = czyste minimum, 90 = zalecane)
    void configure(float windowSec, float stepMs, float tolerancePercent);

    void reset();

    /// Odczyt poziomu w dBFS. Wolany co `stepMs`.
    void addLevel(float levelDb);

    /// Poziom utrzymany w BIEZACYM oknie. kNoData, dopoki okno niepelne.
    float currentDb() const;

    /// Czy okno zdazylo sie wypelnic.
    bool ready() const { return filled_ >= capacity_; }

    size_t capacity() const { return capacity_; }

private:
    size_t binFor(float levelDb) const;

    uint16_t ring_[kMaxSamples] = {};  ///< indeksy koszykow, cyklicznie
    uint16_t bins_[kBins] = {};        ///< ile probek w kazdym koszyku
    size_t capacity_ = kMaxSamples;
    size_t head_ = 0;
    size_t filled_ = 0;
    float tolerance_ = 90.0f;
};

}  // namespace noise
