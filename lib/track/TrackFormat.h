// Motusy Moto Box — format sladu trasy na flashu i na drucie
// (docs/gpx-slad-trasy.md §2, kontrakt w docs/api-slad-trasy.md).
//
// JEDEN FORMAT, NIE DWA. Plik lezacy na flashu jest DOKLADNIE tym, co idzie
// w ciele zadania POST — bajt w bajt, bez przepakowywania. `HTTPClient` umie
// POST-owac ze `Stream`, wiec 40 kB przechodzi prosto z pliku malym buforkiem.
// Gdyby format pliku i format przesylki byly osobne, urzadzenie musialoby
// zbudowac calosc w RAM (nie miesci sie obok handshake'u TLS) albo tlumaczyc
// w locie (dwa miejsca do rozjechania). Stad zapis liniowy i doklejalny.
//
// ZAPIS DZIESIETNY, NIE POLYLINE. Kodowanie Google'a (base64 + varint) daloby
// slad dwa razy mniejszy. Swiadomie z tego rezygnujemy: zapis dziesietny czyta
// sie okiem na porcie USB, pisze przez snprintf, a parsuje przez explode.
// Numer wersji w pierwszej linii zostawia furtke, gdyby to sie zmienilo.
//
// UKLAD PLIKU:
//
//     MMBT1                        <- magia + wersja formatu
//     dev=70041ddc6bc8             <- naglowek, jedna para na linie
//     fw=1.0.0
//     eps=8
//     t0=1757001234                <- czas punktu startowego, unix UTC (0 = nieznany)
//     p0=1957648,5133528           <- punkt startowy, lon,lat w 1e-5 stopnia
//                                  <- pusta linia konczy naglowek
//     113,-182,5,12                <- dlon,dlat,dt[s],przechyl
//     26,-3,1,-31
//     -                            <- przerwa: nastepny punkt zaczyna segment
//     340,84,12,0
//
// KOLEJNOSC lon, lat — zapisana tu, w kontrakcie i w `TrackDecimator`, bo
// pomylka w tym miejscu daje slad gdzies w Somalii i nikt tego nie zauwazy
// od razu.
//
// DELTY WZGLEDEM POPRZEDNIEGO ZAPISANEGO PUNKTU, liczone na int32. Arytmetyka
// jest wtedy dokladna i blad sie NIE KUMULUJE — po szesciu godzinach ostatni
// punkt jest tak samo dobry jak pierwszy.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

#include "TrackDecimator.h"

namespace track {

/// Wersja formatu. Zmiana ukladu linii wymaga podniesienia tej liczby —
/// serwer odrzuca plik z nieznana magia zamiast zgadywac.
constexpr char kFormatMagic[] = "MMBT1";

/// Najdluzsza linia, jaka format moze wyprodukowac. Punkt to cztery liczby:
/// dwie delty (do 11 znakow ze znakiem), dt i przechyl — z zapasem.
constexpr size_t kMaxLineBytes = 48;

/// Naglowek pliku. `seq` NIE jest tu obecny celowo: numer przejazdu nadaje sie
/// dopiero przy archiwizacji, czyli PO zakonczeniu jazdy, a plik powstaje
/// w jej trakcie. Numer idzie w adresie zadania.
struct TrackHeader {
    /// 12 znakow hex z eFuse MAC.
    const char* deviceId = "";
    const char* firmware = "";
    /// Szerokosc korytarza uzyta przy zapisie, w metrach. Bez niej danych nie
    /// da sie pozniej uczciwie porownac miedzy wersjami firmware.
    uint8_t corridorM = 8;
};

/// Sklada kolejne linie pliku. Trzyma poprzedni punkt, bo delty licza sie
/// wzgledem niego — wolajacy ma tylko dopisywac zwrocone bajty do pliku.
class TrackWriter {
public:
    void reset();

    /// Naglowek razem z punktem startowym. Wolac raz, przy PIERWSZYM punkcie
    /// przejazdu — plik powstaje dopiero wtedy, bo przed pierwszym fixem nie
    /// ma czego zapisac.
    ///
    /// Przechyl punktu startowego celowo nie trafia do pliku: punkt niesie
    /// maksimum z odcinka, ktory poprzedza, a przed pierwszym punktem nie ma
    /// zadnego odcinka.
    ///
    /// @param out bufor na linie; potrzeba ~96 bajtow.
    /// @return liczba zapisanych znakow bez konczacego zera; 0 gdy bufor za maly.
    size_t begin(const TrackHeader& header, const Point& first, char* out, size_t outSize);

    /// Kolejny punkt jako delta. Punkt z `startsSegment` dostaje przed soba
    /// linie znacznika przerwy.
    ///
    /// @param out bufor na linie; potrzeba `kMaxLineBytes` + 2.
    /// @return liczba zapisanych znakow bez konczacego zera; 0 gdy bufor za maly
    ///         albo gdy `begin()` jeszcze nie bylo.
    size_t append(const Point& point, char* out, size_t outSize);

    /// Wznawia pisanie do ISTNIEJACEGO pliku: ustawia punkt odniesienia dla
    /// kolejnych delt, nie pisac naglowka drugi raz. Uzywane po restarcie
    /// w trakcie przejazdu — patrz `TrackScanner`.
    void resume(const Point& last);

    bool started() const { return started_; }

private:
    Point previous_{};
    bool started_ = false;
};

/// Czyta zapisany slad linia po linii i odtwarza jego stan koncowy.
///
/// PO CO: restart w trakcie jazdy (zanik zasilania, watchdog, rozladowana
/// bateria) zostawia na flashu plik bez numeru przejazdu. Kasowanie go byloby
/// najprostsze i najgorsze — w piatej godzinie trasy kosztowaloby cala trase,
/// czyli dokladnie to, przed czym mial chronic zapis na flash zamiast do RAM.
/// Zeby pisac dalej, trzeba znac ostatni zapisany punkt: delty licza sie
/// wzgledem niego, a w pliku sa tylko przyrosty.
///
/// Skanera uzywa sie takze do sprawdzenia, czy plik w ogole jest caly:
/// linia, ktorej nie da sie przeczytac, konczy skanowanie. Przy zapisie
/// sekwencyjnym uszkodzona moze byc wylacznie OSTATNIA linia (przerwany zapis),
/// wiec wszystko przed nia jest dobre i warte zachowania.
class TrackScanner {
public:
    void reset();

    /// Podaje jedna linie pliku, BEZ znaku konca linii.
    /// @return false gdy linia jest niepoprawna — od tego miejsca plik
    ///         nadaje sie tylko do obciecia.
    bool feedLine(const char* line);

    /// Czy naglowek zostal przeczytany w calosci i plik ma punkt startowy.
    bool ready() const { return ready_; }

    /// Ostatni zapisany punkt. Sensowny dopiero przy `ready()`.
    const Point& last() const { return last_; }

    /// Czy slad niesie prawdziwy czas UTC (`t0` rozne od zera). Wznawiajac,
    /// trzeba zostac przy tym samym trybie: serwer traktuje `t0=0` jako
    /// "caly slad bez czasu", wiec mieszanka nie ma jak zaistniec.
    bool timed() const { return timed_; }

    /// Ile punktow (bez startowego) juz jest w pliku.
    uint32_t points() const { return points_; }

private:
    bool sawMagic_ = false;
    bool inHeader_ = true;
    bool ready_ = false;
    bool timed_ = false;

    int32_t startLon_ = 0;
    int32_t startLat_ = 0;
    uint32_t startTime_ = 0;
    bool sawStart_ = false;

    Point last_{};
    uint32_t points_ = 0;
};

}  // namespace track
