// Motusy Moto Box — parser zdan NMEA 0183 z modulu GPS.
//
// Czyste C++ bez zaleznosci od Arduino, wiec to samo zrodlo kompiluje sie na
// urzadzeniu i w testach natywnych (architektura §10). Warstwa sprzetowa
// (src/hal/GpsSource) tylko podaje bajty odebrane z UART-u.
//
// Z calego NMEA potrzebujemy dwoch zdan:
//   RMC — status fixa (A/V) i predkosc nad ziemia w wezlach,
//   GGA — liczba satelitow i HDOP, czyli material do oceny jakosci fixa.
//
// OCENA JAKOSCI FIXA MIESZKA TUTAJ. Architektura §3a mowi wprost: `RideMetrics`
// dostaje gotowa flage `valid` i nie zgaduje. Samo odebranie zdania to za malo —
// odbiornik nadaje RMC takze wtedy, gdy nie ma jeszcze zadnego fixa.
//
// SUMA KONTROLNA JEST OBOWIAZKOWA. Poza oczywista ochrona przed przeklamaniem
// pelni druga role: przy zlej predkosci transmisji z UART-u leca smieci, ktore
// czasem wygladaja jak zdanie. Licznik `validSentences()` jest jedynym pewnym
// dowodem, ze czytamy modul, a nie szum — na tym opiera sie dobor predkosci
// transmisji w GpsSource.

#pragma once

#include <cstddef>
#include <cstdint>

namespace gps {

/// Prog, powyzej ktorego fix uznajemy za nadajacy sie do pomiaru.
struct NmeaQuality {
    /// Ponizej czterech satelitow pozycja nie ma rozwiazania w trzech wymiarach,
    /// a predkosc potrafi skakac o kilkadziesiat km/h.
    uint8_t minSatellites = 4;
    /// Rozmycie pozycji. Powyzej tej wartosci odbiornik sam przyznaje,
    /// ze geometria satelitow jest zla.
    float maxHdop = 5.0f;
};

struct NmeaFix {
    /// Fix potwierdzony i spelniajacy progi jakosci. Tylko wtedy `speedKmh`
    /// jest pomiarem.
    bool valid = false;
    float speedKmh = 0.0f;
    uint8_t satellites = 0;
    /// 0 oznacza "nieznane" — modul nie przyslal jeszcze GGA.
    float hdop = 0.0f;
};

/// Typ zdania, ktore wlasnie sie domknelo.
enum class Sentence : uint8_t {
    None,
    /// Nowa probka predkosci — RMC niesie status fixa i predkosc.
    Rmc,
    /// Aktualizacja jakosci fixa (satelity, HDOP).
    Gga,
    Other,
};

class NmeaParser {
public:
    /// NMEA 0183 ogranicza zdanie do 82 znakow; zapas na moduly, ktore
    /// tego nie przestrzegaja co do znaku.
    static constexpr size_t kMaxSentence = 96;

    explicit NmeaParser(const NmeaQuality& quality = {}) : quality_(quality) {}

    void setQuality(const NmeaQuality& quality) { quality_ = quality; }

    /// Podaje jeden znak z UART-u.
    /// @return typ zdania, ktore wlasnie sie domknelo I przeszlo sume kontrolna.
    ///         Dla wszystkich pozostalych znakow Sentence::None.
    Sentence feed(char c);

    const NmeaFix& fix() const { return fix_; }

    uint32_t validSentences() const { return validSentences_; }
    /// Zdania odrzucone: zla suma kontrolna albo dlugosc powyzej bufora.
    /// Przy zlej predkosci transmisji rosnie szybciej niz licznik poprawnych.
    uint32_t rejectedSentences() const { return rejectedSentences_; }

    /// Kasuje stan i liczniki. Wolane przy zmianie ustawien UART-u, zeby
    /// smieci z poprzedniej proby nie liczyly sie do nastepnej.
    void reset();

private:
    Sentence finishSentence();
    Sentence parseSentence();
    void parseRmc();
    void parseGga();
    /// Kopiuje pole o zadanym numerze (0 = typ zdania) do `out`.
    /// @return false gdy pola nie ma — wtedy `out` jest pustym napisem.
    bool field(size_t index, char* out, size_t size) const;
    bool qualityOk() const;

    NmeaQuality quality_{};
    NmeaFix fix_{};

    char buffer_[kMaxSentence + 1] = {};
    size_t length_ = 0;
    bool collecting_ = false;
    /// Zdanie przekroczylo bufor — zbieramy je do konca linii i wyrzucamy.
    bool overflow_ = false;

    /// Ostatnie wartosci z GGA. Trzymane osobno, bo RMC i GGA przychodza
    /// naprzemiennie, a ocena jakosci zapada w chwili RMC.
    uint8_t satellites_ = 0;
    float hdop_ = 0.0f;
    /// Czy modul w ogole nadaje GGA. Jesli nie — nie mamy czym ocenic jakosci
    /// i zostaje sam status z RMC.
    bool sawGga_ = false;

    uint32_t validSentences_ = 0;
    uint32_t rejectedSentences_ = 0;
};

}  // namespace gps
