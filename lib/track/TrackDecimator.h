// Motusy Moto Box — decymator sladu trasy (docs/gpx-slad-trasy.md §3).
//
// PROBLEM. Modul oddaje fix raz na sekunde. Szesciogodzinna trasa to 21 600
// punktow i ~230 kB, a ogromna ich wiekszosc lezy na prostej i nie wnosi nic.
//
// ODRZUCONE: progi na predkosc i "ostrosc zakretu" (prosta -> co 10 s, miasto
// -> co 5 s, zakret -> co 1 s). Takie progi stroi sie w nieskonczonosc i tak
// nie trafiaja w przypadki brzegowe: lagodny luk brany 90 km/h wyglada jak
// prosta, a scina sie widocznie.
//
// ROZWIAZANIE: jedno kryterium, wyprowadzone z geometrii, a nie z progow —
// O ILE METROW NARYSOWANA LINIA ROZJEDZIE SIE Z PRAWDZIWYM TOREM JAZDY.
//
//   1. Trzymamy kotwice A (ostatni ZAPISANY punkt) i bufor fixow po niej.
//   2. Dla nowego fixu P liczymy odchylenie kazdego punktu z bufora od
//      odcinka A->P.
//   3. Ktorys wyszedl poza korytarz -> ostatni fix przed P zostaje zapisany
//      jako nowa kotwica, bufor czyscimy.
//
// GWARANCJA: kazdy ODRZUCONY fix lezy nie dalej niz eps od zapisanej lamanej.
// Wynika to z kolejnosci sprawdzen — fix trafia do bufora dopiero wtedy, gdy
// przeszedl test wzgledem odcinka konczacego sie na nim samym, wiec w chwili
// zapisu kotwicy wszystkie wczesniejsze punkty byly juz wzgledem niej mierzone.
// Progi takiej gwarancji nie daja i to jest wlasciwy powod tego wyboru, a nie
// oszczednosc bajtow: korytarz i staly odstep wychodza na remis w rozmiarze,
// roznica jest w tym, GDZIE te bajty sa wydane. Autostrada dostaje punkt co
// minute, ciasny zakret co sekunde, i skaluje sie z predkoscia sam z siebie.
//
// EPSILON 8 m, nie 5 m — przy 5 m sam szum GPS na prostej wyzwala zapisy.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

namespace track {

/// Jeden odczyt pozycji, juz przeliczony na jednostki 1e-5 stopnia.
///
/// DLACZEGO LICZBY CALKOWITE: delty licza sie wtedy dokladnie i blad sie NIE
/// KUMULUJE — po szesciu godzinach ostatni punkt jest tak samo dobry jak
/// pierwszy. Delty liczone z wartosci zmiennoprzecinkowych powodowalyby
/// powolny odplyw sladu.
struct Fix {
    /// Kolejnosc lon, lat — zapisana tu i w kontrakcie, bo pomylka w tym
    /// miejscu daje slad gdzies w Somalii i nikt tego nie zauwazy od razu.
    int32_t lonE5 = 0;
    int32_t latE5 = 0;
    /// Sekundy. Zrodlo (czas UTC albo od startu przejazdu) wybiera wolajacy —
    /// decymator uzywa tylko roznic.
    uint32_t timeS = 0;
    /// Przechyl ze znakiem w chwili fixu, + = w prawo.
    int8_t leanDeg = 0;
};

/// Punkt, ktory trafia do sladu.
struct Point {
    int32_t lonE5 = 0;
    int32_t latE5 = 0;
    uint32_t timeS = 0;
    /// NAJWIEKSZY przechyl na odcinku od poprzedniego zapisanego punktu do
    /// tego, ze znakiem. Nie chwilowy odczyt: punkt reprezentuje caly odcinek,
    /// a korytarz zageszcza punkty wlasnie tam, gdzie przechyl ma znaczenie.
    int8_t leanDeg = 0;
    /// Ten punkt zaczyna nowy segment — miedzy nim a poprzednim slad sie urwal
    /// (tunel, wiadukt, garaz, postoj). GPX ma na to <trkseg>.
    bool startsSegment = false;
};

struct TrackDecimatorConfig {
    /// Szerokosc korytarza w metrach.
    float corridorM = 8.0f;
    /// Twardy ogranicznik z dolu: punkt nie rzadziej niz co tyle sekund.
    /// Bez niego stanie w miejscu nie zapisuje niczego przez cala postoj.
    uint32_t maxGapS = 60;
    /// Twardy ogranicznik z gory: nie czesciej niz raz na tyle sekund.
    /// Przy module oddajacym 1 Hz nie ma znaczenia; chroni przed modulem
    /// ustawionym na 10 Hz, ktory zalalby slad.
    uint32_t minGapS = 1;
};

class TrackDecimator {
public:
    /// Bufor pokrywa `maxGapS` przy 1 Hz z zapasem. Przepelnienie wymusza
    /// zapis punktu — lepiej zapisac za duzo niz zgubic kawalek trasy.
    static constexpr size_t kMaxBuffer = 96;

    explicit TrackDecimator(const TrackDecimatorConfig& config = {}) : config_(config) {}

    void setConfig(const TrackDecimatorConfig& config) { config_ = config; }

    /// Nowy przejazd: zapomina kotwice, bufor i przelicznik dlugosci.
    void reset();

    /// Podaje kolejny fix.
    /// @param out wypelniane tylko gdy funkcja zwroci true.
    /// @return true gdy powstal punkt do zapisania w sladzie.
    bool update(const Fix& fix, Point& out);

    /// Domyka biezacy segment: oddaje ostatni buforowany fix, zeby slad konczyl
    /// sie tam, gdzie motocykl faktycznie stanal, a nie na ostatniej kotwicy.
    /// @return false gdy nie ma czego domykac.
    bool flush(Point& out);

    /// Zglasza nieciaglosc (utrata fixu, postoj). Nastepny zapisany punkt
    /// dostanie `startsSegment`. Wolac PO `flush()`, inaczej ogon segmentu
    /// przepada — ta para wywolan jest calym mechanizmem segmentow.
    void breakSegment();

    /// Ile fixow czeka w buforze. Do diagnostyki i testow.
    size_t buffered() const { return count_; }

private:
    /// Zapisuje `buffer_[index]` jako nowa kotwice; fixy po nim zostaja
    /// w buforze. Przechyl punktu to maksimum z odrzuconego odcinka.
    void emitAt(size_t index, Point& out);
    /// Odleglosc punktu od odcinka kotwica->`to`, w metrach.
    float deviationM(const Fix& point, const Fix& to) const;

    TrackDecimatorConfig config_{};

    Fix anchor_{};
    bool haveAnchor_ = false;
    bool pendingBreak_ = true;

    Fix buffer_[kMaxBuffer]{};
    size_t count_ = 0;

    /// Metry na jednostke 1e-5 stopnia dlugosci. Liczone RAZ na przejazd:
    /// na 200 km trasy cos(lat) zmienia sie o ~2%, co przy korytarzu 8 m nie
    /// ma znaczenia, a pozwala uniknac cosinusa przy kazdym fixie.
    float metersPerUnitLon_ = 0.0f;
};

}  // namespace track
