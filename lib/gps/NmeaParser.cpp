#include "NmeaParser.h"

#include <cstdlib>
#include <cstring>

namespace gps {
namespace {

/// Dwie cyfry dziesietne spod wskazanej pozycji; -1 gdy to nie sa cyfry.
int twoDigits(const char* text, size_t offset) {
    const char high = text[offset];
    const char low = text[offset + 1];
    if (high < '0' || high > '9' || low < '0' || low > '9') return -1;
    return (high - '0') * 10 + (low - '0');
}

/// Dni od 1970-01-01 dla daty kalendarzowej (algorytm Howarda Hinnanta).
/// Kalendarz gregorianski w calosci, bez tablic i bez zaleznosci od <ctime>,
/// zeby ten sam kod liczyl tak samo na urzadzeniu i w testach natywnych.
long long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

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

    // Pole 1: czas UTC "hhmmss.ss", pole 9: data "ddmmrr".
    char timeField[16];
    field(1, timeField, sizeof(timeField));
    field(9, value, sizeof(value));
    fix_.unixTime = parseDateTime(timeField, value);

    // Pole 2: status. 'A' = dane wiarygodne, 'V' = ostrzezenie (brak fixa).
    field(2, value, sizeof(value));
    const bool statusOk = value[0] == 'A';

    // Pola 3-6: szerokosc, polkula N/S, dlugosc, polkula E/W.
    char coord[16];
    char hemisphere[8];

    int32_t latE5 = 0;
    field(3, coord, sizeof(coord));
    field(4, hemisphere, sizeof(hemisphere));
    const bool latOk = parseCoordinate(coord, hemisphere[0], 9000000, latE5);

    int32_t lonE5 = 0;
    field(5, coord, sizeof(coord));
    field(6, hemisphere, sizeof(hemisphere));
    const bool lonOk = parseCoordinate(coord, hemisphere[0], 18000000, lonE5);

    // Pole 7: predkosc nad ziemia w wezlach.
    field(7, value, sizeof(value));
    const float knots = static_cast<float>(std::strtod(value, nullptr));

    fix_.satellites = satellites_;
    fix_.hdop = hdop_;
    // Pozycja jest czescia fixu na rowni ze statusem: zdanie ze statusem 'A',
    // ale z pustymi polami wspolrzednych, nie opisuje miejsca i nie ma prawa
    // trafic do sladu.
    fix_.valid = statusOk && latOk && lonOk && qualityOk();
    // 1 wezel = 1,852 km/h dokladnie — z definicji mili morskiej.
    fix_.speedKmh = fix_.valid ? knots * 1.852f : 0.0f;
    fix_.lonE5 = fix_.valid ? lonE5 : 0;
    fix_.latE5 = fix_.valid ? latE5 : 0;
}

bool NmeaParser::parseCoordinate(const char* text, char hemisphere, int32_t maxE5,
                                 int32_t& out) {
    // LICZONE NA LICZBACH CALKOWITYCH, nie przez strtod. Wynik ma siedem cyfr
    // znaczacych (np. 5133528), a float ma ich okolo siedmiu — konwersja przez
    // float gubilaby ostatnia, czyli okolo metra, przy KAZDYM fixie. Blad
    // trafialby potem do kazdej delty sladu.
    const size_t length = std::strlen(text);
    if (length == 0) return false;

    const char* dot = std::strchr(text, '.');
    const size_t intLength = dot != nullptr ? static_cast<size_t>(dot - text) : length;

    // Format to zawsze stopnie + DWIE cyfry minut, wiec ostatnie dwie cyfry
    // czesci calkowitej to minuty, a wszystko przed nimi to stopnie. Dzieki
    // temu ta sama funkcja obsluguje szerokosc (ddmm) i dlugosc (dddmm).
    if (intLength < 3 || intLength > 5) return false;

    int32_t degrees = 0;
    for (size_t i = 0; i + 2 < intLength; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        degrees = degrees * 10 + (text[i] - '0');
    }

    int32_t minutesE4 = 0;
    for (size_t i = intLength - 2; i < intLength; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        minutesE4 = minutesE4 * 10 + (text[i] - '0');
    }
    if (minutesE4 >= 60) return false;
    minutesE4 *= 10000;

    // Czesc ulamkowa minut, znormalizowana do czterech cyfr. Odbiorniki podaja
    // ich cztery albo piec — nadmiar obcinamy, niedomiar dopelniamy zerami.
    if (dot != nullptr) {
        int32_t scale = 1000;
        for (const char* p = dot + 1; *p != '\0' && scale > 0; ++p) {
            if (*p < '0' || *p > '9') return false;
            minutesE4 += (*p - '0') * scale;
            scale /= 10;
        }
    }

    // 1e-5 stopnia = minuty / 60 * 1e5, a minuty mamy w jednostkach 1e-4,
    // wiec caly przelicznik to dzielenie przez szesc. Plus trzy zaokragla
    // do najblizszej jednostki zamiast obcinac w dol.
    const int32_t value = degrees * 100000 + (minutesE4 + 3) / 6;

    if (hemisphere == 'S' || hemisphere == 'W') {
        out = -value;
    } else if (hemisphere == 'N' || hemisphere == 'E') {
        out = value;
    } else {
        return false;  // brak polkuli — nie wiadomo, po ktorej stronie swiata
    }

    // Poza zakresem zdanie jest uszkodzone, a nie egzotyczne.
    if (out > maxE5 || out < -maxE5) return false;
    return true;
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

uint32_t NmeaParser::parseDateTime(const char* time, const char* date) {
    // Odbiornik bez synchronizacji czasu zostawia te pola puste — wtedy nie ma
    // czego liczyc i zostaje zero, czyli "data nieznana".
    if (std::strlen(time) < 6 || std::strlen(date) < 6) return 0;

    const int hour = twoDigits(time, 0);
    const int minute = twoDigits(time, 2);
    const int second = twoDigits(time, 4);
    const int day = twoDigits(date, 0);
    const int month = twoDigits(date, 2);
    const int year = twoDigits(date, 4);

    if (hour < 0 || minute < 0 || second < 0 || day < 0 || month < 0 || year < 0) return 0;
    // Sekunda 60 wystepuje przy sekundzie przestepnej i jest poprawna.
    if (hour > 23 || minute > 59 || second > 60) return 0;
    if (day < 1 || day > 31 || month < 1 || month > 12) return 0;

    // Rok dwucyfrowy. NMEA nie niesie stulecia, a moduly liczace od 1980
    // i tak nie doczekaja 2080 — zakres 2000-2099 wystarcza z zapasem.
    const long long days = daysFromCivil(2000 + year, static_cast<unsigned>(month),
                                         static_cast<unsigned>(day));
    const long long epoch = days * 86400LL + hour * 3600LL + minute * 60LL + second;
    if (epoch <= 0) return 0;
    return static_cast<uint32_t>(epoch);
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
