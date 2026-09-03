// Motusy Moto Box — testy parsera NMEA (srodowisko native).
//
// Uruchomienie:  pio test -e native
//
// Sumy kontrolne liczy tu helper `sentence()`, a nie czlowiek. Wpisana recznie
// sumy nie da sie odroznic od bledu w parserze: oba przypadki wygladaja tak
// samo — zdanie odrzucone.

#include <unity.h>

#include <cstdio>
#include <string>

#include "NmeaParser.h"

using namespace gps;

void setUp() {}
void tearDown() {}

namespace {

/// Sklada kompletne zdanie NMEA z poprawna suma kontrolna.
std::string sentence(const std::string& body) {
    uint8_t sum = 0;
    for (char c : body) sum ^= static_cast<uint8_t>(c);

    char checksum[8];
    std::snprintf(checksum, sizeof(checksum), "*%02X\r\n", sum);
    return "$" + body + checksum;
}

/// Podaje caly napis do parsera. @return typ ostatniego domknietego zdania.
Sentence feedAll(NmeaParser& parser, const std::string& data) {
    Sentence last = Sentence::None;
    for (char c : data) {
        const Sentence result = parser.feed(c);
        if (result != Sentence::None) last = result;
    }
    return last;
}

/// GGA z fixem: 8 satelitow, HDOP 0,9.
std::string goodGga() {
    return sentence("GNGGA,120000.00,5008.1234,N,01925.4321,E,1,08,0.9,220.0,M,40.0,M,,");
}

/// RMC z fixem i predkoscia zadana w wezlach.
std::string rmcWithKnots(const char* knots, char status = 'A') {
    std::string body = "GNRMC,120000.00,";
    body += status;
    body += ",5008.1234,N,01925.4321,E,";
    body += knots;
    body += ",054.7,030926,,,A";
    return sentence(body);
}

void test_rmc_with_fix_gives_speed_in_kmh() {
    NmeaParser parser;
    feedAll(parser, goodGga());
    const Sentence last = feedAll(parser, rmcWithKnots("50.000"));

    TEST_ASSERT_EQUAL(static_cast<int>(Sentence::Rmc), static_cast<int>(last));
    TEST_ASSERT_TRUE(parser.fix().valid);
    // 50 wezlow = 92,6 km/h.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 92.6f, parser.fix().speedKmh);
    TEST_ASSERT_EQUAL_UINT8(8, parser.fix().satellites);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f, parser.fix().hdop);
    TEST_ASSERT_EQUAL_UINT32(2, parser.validSentences());
    TEST_ASSERT_EQUAL_UINT32(0, parser.rejectedSentences());
}

/// Zdanie odbierane, ale bez fixa: status 'V'. Predkosc z takiego zdania jest
/// przypadkowa i nie moze trafic do rekordow.
void test_rmc_without_fix_is_not_valid() {
    NmeaParser parser;
    feedAll(parser, goodGga());
    feedAll(parser, rmcWithKnots("50.000", 'V'));

    TEST_ASSERT_FALSE(parser.fix().valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, parser.fix().speedKmh);
    // Zdanie bylo poprawne — to nie jest blad transmisji.
    TEST_ASSERT_EQUAL_UINT32(2, parser.validSentences());
}

/// Trzy satelity to rozwiazanie plaskie; predkosc potrafi wtedy skakac
/// o kilkadziesiat km/h i ustanowic rekord, ktorego nie bylo.
void test_too_few_satellites_blocks_fix() {
    NmeaParser parser;
    feedAll(parser, sentence("GNGGA,120000.00,5008.1234,N,01925.4321,E,1,03,1.2,220.0,M,40.0,M,,"));
    feedAll(parser, rmcWithKnots("50.000"));

    TEST_ASSERT_FALSE(parser.fix().valid);
    TEST_ASSERT_EQUAL_UINT8(3, parser.fix().satellites);
}

/// Duze HDOP to przyznanie sie odbiornika do zlej geometrii satelitow.
void test_high_hdop_blocks_fix() {
    NmeaParser parser;
    feedAll(parser, sentence("GNGGA,120000.00,5008.1234,N,01925.4321,E,1,09,9.9,220.0,M,40.0,M,,"));
    feedAll(parser, rmcWithKnots("50.000"));

    TEST_ASSERT_FALSE(parser.fix().valid);
}

/// GGA z jakoscia 0 zeruje satelity — inaczej odczyt sprzed wjazdu do tunelu
/// przepuscilby pierwsze RMC po wyjezdzie, zanim wroci prawdziwe rozwiazanie.
void test_lost_fix_clears_quality() {
    NmeaParser parser;
    feedAll(parser, goodGga());
    feedAll(parser, rmcWithKnots("50.000"));
    TEST_ASSERT_TRUE(parser.fix().valid);

    feedAll(parser, sentence("GNGGA,120001.00,,,,,0,00,99.99,,,,,,"));
    feedAll(parser, rmcWithKnots("50.000"));

    TEST_ASSERT_FALSE(parser.fix().valid);
    TEST_ASSERT_EQUAL_UINT8(0, parser.fix().satellites);
}

void test_bad_checksum_is_rejected() {
    NmeaParser parser;
    feedAll(parser, goodGga());
    // Ta sama tresc, suma podmieniona na bledna.
    feedAll(parser, "$GNRMC,120000.00,A,5008.1234,N,01925.4321,E,50.000,054.7,030926,,,A*00\r\n");

    TEST_ASSERT_FALSE(parser.fix().valid);
    TEST_ASSERT_EQUAL_UINT32(1, parser.validSentences());
    TEST_ASSERT_EQUAL_UINT32(1, parser.rejectedSentences());
}

/// Tak wyglada strumien przy zle dobranej predkosci transmisji: same smieci.
/// Parser ma to policzyc jako odrzucone i pozostac zdolny do pracy — na tym
/// liczniku opiera sie dobor ustawien portu w hal::GpsSource.
void test_garbage_then_valid_sentence() {
    NmeaParser parser;
    std::string garbage = "\xFF~~$ABC\r\n\xC3 nonsens\r\n$";
    garbage.push_back('\0');  // przy zlym baudzie zdarza sie i bajt zerowy
    garbage += "\x7F\x02*ZZ\r\n";
    feedAll(parser, garbage);

    TEST_ASSERT_EQUAL_UINT32(0, parser.validSentences());
    TEST_ASSERT_TRUE(parser.rejectedSentences() > 0);

    feedAll(parser, goodGga());
    feedAll(parser, rmcWithKnots("10.000"));

    TEST_ASSERT_TRUE(parser.fix().valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 18.52f, parser.fix().speedKmh);
}

/// Zdanie dluzsze niz bufor nie moze rozsypac synchronizacji: nastepne
/// poprawne zdanie ma sie sparsowac normalnie.
void test_overlong_sentence_does_not_break_parser() {
    NmeaParser parser;
    std::string flood = "$GNGGA";
    flood.append(NmeaParser::kMaxSentence + 40, 'X');
    flood += "\r\n";
    feedAll(parser, flood);

    TEST_ASSERT_EQUAL_UINT32(0, parser.validSentences());
    TEST_ASSERT_EQUAL_UINT32(1, parser.rejectedSentences());

    feedAll(parser, goodGga());
    feedAll(parser, rmcWithKnots("0.000"));
    TEST_ASSERT_TRUE(parser.fix().valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, parser.fix().speedKmh);
}

/// Zdanie producenta ($PGRMC) ma "RMC" dokladnie tam, gdzie zdanie standardowe
/// — a zupelnie inne pola. Wziecie go za RMC dawaloby losowa predkosc.
void test_proprietary_sentence_is_not_taken_for_rmc() {
    NmeaParser parser;
    feedAll(parser, goodGga());
    feedAll(parser, rmcWithKnots("50.000"));

    const Sentence last = feedAll(parser, sentence("PGRMC,A,,3,,,,,W,N,3,2,1,30"));

    TEST_ASSERT_EQUAL(static_cast<int>(Sentence::Other), static_cast<int>(last));
    // Poprzedni pomiar bez zmian.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 92.6f, parser.fix().speedKmh);
}

/// Odbiornik nadajacy same RMC (bez GGA) nie daje czym ocenic jakosci —
/// wtedy zostaje sam status, zamiast blokady na zawsze.
void test_rmc_without_gga_relies_on_status() {
    NmeaParser parser;
    feedAll(parser, rmcWithKnots("20.000"));

    TEST_ASSERT_TRUE(parser.fix().valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 37.04f, parser.fix().speedKmh);
}

/// Zdanie moze przyjsc w kawalkach — bufor UART-u nie zna granic zdan.
void test_sentence_split_across_reads() {
    NmeaParser parser;
    const std::string gga = goodGga();
    const std::string rmc = rmcWithKnots("50.000");

    feedAll(parser, gga.substr(0, 10));
    feedAll(parser, gga.substr(10));
    feedAll(parser, rmc.substr(0, 5));
    feedAll(parser, rmc.substr(5, 20));
    feedAll(parser, rmc.substr(25));

    TEST_ASSERT_TRUE(parser.fix().valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 92.6f, parser.fix().speedKmh);
}

}  // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rmc_with_fix_gives_speed_in_kmh);
    RUN_TEST(test_rmc_without_fix_is_not_valid);
    RUN_TEST(test_too_few_satellites_blocks_fix);
    RUN_TEST(test_high_hdop_blocks_fix);
    RUN_TEST(test_lost_fix_clears_quality);
    RUN_TEST(test_bad_checksum_is_rejected);
    RUN_TEST(test_garbage_then_valid_sentence);
    RUN_TEST(test_overlong_sentence_does_not_break_parser);
    RUN_TEST(test_proprietary_sentence_is_not_taken_for_rmc);
    RUN_TEST(test_rmc_without_gga_relies_on_status);
    RUN_TEST(test_sentence_split_across_reads);
    return UNITY_END();
}
