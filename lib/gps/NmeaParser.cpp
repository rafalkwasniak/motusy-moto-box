#include "NmeaParser.h"

#include <cstdlib>
#include <cstring>

namespace gps {
namespace {

/// @return wartosc cyfry szesnastkowej albo -1, gdy to nie jest cyfra.
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

}  // namespace

Sentence NmeaParser::feed(char c) {
    // Dolar zawsze otwiera nowe zdanie. Dzieki temu przerwana transmisja
    // (albo smieci przy zlej predkosci) kosztuje jedno zdanie, a nie synchronizacje.
    if (c == '$') {
        collecting_ = true;
        overflow_ = false;
        length_ = 0;
        return Sentence::None;
    }

    if (!collecting_) return Sentence::None;

    if (c == '\r' || c == '\n') return finishSentence();

    if (length_ >= kMaxSentence) {
        overflow_ = true;
        return Sentence::None;
    }

    buffer_[length_++] = c;
    return Sentence::None;
}

Sentence NmeaParser::finishSentence() {
    collecting_ = false;
    buffer_[length_] = '\0';

    if (overflow_) {
        overflow_ = false;
        ++rejectedSentences_;
        return Sentence::None;
    }

    if (length_ == 0) return Sentence::None;

    // Format sumy kontrolnej jest sztywny: "*HH" na koncu zdania.
    if (length_ < 4 || buffer_[length_ - 3] != '*') {
        ++rejectedSentences_;
        return Sentence::None;
    }

    const size_t star = length_ - 3;
    uint8_t sum = 0;
    for (size_t i = 0; i < star; ++i) {
        sum ^= static_cast<uint8_t>(buffer_[i]);
    }

    const int high = hexDigit(buffer_[star + 1]);
    const int low = hexDigit(buffer_[star + 2]);
    if (high < 0 || low < 0 || sum != static_cast<uint8_t>((high << 4) | low)) {
        ++rejectedSentences_;
        return Sentence::None;
    }

    // Suma jest juz sprawdzona i tylko przeszkadza w podziale na pola.
    buffer_[star] = '\0';
    length_ = star;
    ++validSentences_;

    return parseSentence();
}

Sentence NmeaParser::parseSentence() {
    // "GNRMC,..." — dwa znaki nadawcy, trzy znaki typu, przecinek.
    // Zdania producenta ($PGRMC i podobne) maja inny uklad, a przypadkowo
    // trafiaja w te sama pozycje typu — stad odrzucenie prefiksu "P".
    if (length_ < 6 || buffer_[5] != ',' || buffer_[0] == 'P') return Sentence::Other;

    const char* type = buffer_ + 2;

    if (std::strncmp(type, "RMC", 3) == 0) {
        parseRmc();
        return Sentence::Rmc;
    }

    if (std::strncmp(type, "GGA", 3) == 0) {
        parseGga();
        return Sentence::Gga;
    }

    return Sentence::Other;
}

void NmeaParser::parseRmc() {
    char value[16];

    // Pole 2: status. 'A' = dane wiarygodne, 'V' = ostrzezenie (brak fixa).
    field(2, value, sizeof(value));
    const bool statusOk = value[0] == 'A';

    // Pole 7: predkosc nad ziemia w wezlach.
    field(7, value, sizeof(value));
    const float knots = static_cast<float>(std::strtod(value, nullptr));

    fix_.satellites = satellites_;
    fix_.hdop = hdop_;
    fix_.valid = statusOk && qualityOk();
    // 1 wezel = 1,852 km/h dokladnie — z definicji mili morskiej.
    fix_.speedKmh = fix_.valid ? knots * 1.852f : 0.0f;
}

void NmeaParser::parseGga() {
    char value[16];
    sawGga_ = true;

    // Pole 6: jakosc fixa (0 = brak), 7: liczba satelitow, 8: HDOP.
    field(6, value, sizeof(value));
    const int quality = std::atoi(value);

    field(7, value, sizeof(value));
    satellites_ = static_cast<uint8_t>(std::atoi(value));

    field(8, value, sizeof(value));
    hdop_ = static_cast<float>(std::strtod(value, nullptr));

    // Bez fixa liczba satelitow z GGA opisuje tylko "ile widze", nie
    // "ile uzywam". Zerowanie pilnuje, zeby stary odczyt nie przepuscil
    // pierwszego RMC po wyjezdzie z tunelu, zanim wroci prawdziwe rozwiazanie.
    if (quality == 0) {
        satellites_ = 0;
        hdop_ = 0.0f;
    }
}

bool NmeaParser::field(size_t index, char* out, size_t size) const {
    out[0] = '\0';
    if (size == 0) return false;

    size_t current = 0;
    size_t i = 0;
    while (current < index && i < length_) {
        if (buffer_[i] == ',') ++current;
        ++i;
    }
    if (current != index) return false;

    size_t written = 0;
    while (i < length_ && buffer_[i] != ',') {
        if (written + 1 < size) out[written++] = buffer_[i];
        ++i;
    }
    out[written] = '\0';
    return true;
}

bool NmeaParser::qualityOk() const {
    // Modul, ktory nie nadaje GGA, nie daje nam czym oceniac jakosci —
    // zostaje sam status z RMC. Lepsze niz odrzucanie wszystkiego.
    if (!sawGga_) return true;
    if (satellites_ < quality_.minSatellites) return false;
    // HDOP == 0 znaczy "nieznane", nie "idealne".
    if (hdop_ > 0.0f && hdop_ > quality_.maxHdop) return false;
    return true;
}

void NmeaParser::reset() {
    fix_ = NmeaFix{};
    length_ = 0;
    collecting_ = false;
    overflow_ = false;
    satellites_ = 0;
    hdop_ = 0.0f;
    sawGga_ = false;
    validSentences_ = 0;
    rejectedSentences_ = 0;
}

}  // namespace gps
