// Motusy Moto Box — testy formatu sladu trasy (srodowisko native).
//
// Uruchomienie:  pio test -e native
//
// Dwa rodzaje testow, oba potrzebne:
//   1. ZNAK W ZNAK — plik lezacy na flashu idzie w ciele zadania bajt w bajt,
//      wiec kontrakt w docs/api-slad-trasy.md opisuje dokladnie te napisy.
//      Ten sam rygor co w test_telemetry dla JSON-a.
//   2. ODTWORZENIE — delty zsumowane z powrotem musza dac dokladnie te same
//      wspolrzedne, ktore wypuscil decymator. To jest cala racja bytu
//      liczb calkowitych zamiast zmiennoprzecinkowych.

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "TrackDecimator.h"
#include "TrackFormat.h"

using namespace track;

namespace {

TrackHeader testHeader() {
    TrackHeader header;
    header.deviceId = "70041ddc6bc8";
    header.firmware = "1.0.0";
    header.corridorM = 8;
    return header;
}

Point pointAt(int32_t lon, int32_t lat, uint32_t timeS, int8_t lean = 0,
              bool startsSegment = false) {
    Point point;
    point.lonE5 = lon;
    point.latE5 = lat;
    point.timeS = timeS;
    point.leanDeg = lean;
    point.startsSegment = startsSegment;
    return point;
}

}  // namespace

// ── Znak w znak ────────────────────────────────────────────────────────────

void test_naglowek_znak_w_znak() {
    TrackWriter writer;
    char out[128];

    const size_t len =
        writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), out, sizeof(out));

    const char* expected =
        "MMBT1\n"
        "dev=70041ddc6bc8\n"
        "fw=1.0.0\n"
        "eps=8\n"
        "t0=1757001234\n"
        "p0=1957648,5133528\n"
        "\n";

    TEST_ASSERT_EQUAL_STRING(expected, out);
    TEST_ASSERT_EQUAL_UINT32(std::strlen(expected), len);
}

void test_punkt_jako_delta_znak_w_znak() {
    TrackWriter writer;
    char header[128];
    char out[64];

    writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), header, sizeof(header));

    // Ujemna delta szerokosci — jazda na poludnie; przechyl w prawo.
    const size_t len =
        writer.append(pointAt(1957761, 5133346, 1757001239, 12), out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("113,-182,5,12\n", out);
    TEST_ASSERT_EQUAL_UINT32(14, len);
}

void test_przechyl_w_lewo_idzie_ze_znakiem() {
    TrackWriter writer;
    char header[128];
    char out[64];

    writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), header, sizeof(header));
    writer.append(pointAt(1957674, 5133525, 1757001235, -31), out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("26,-3,1,-31\n", out);
}

void test_przerwa_dopisuje_znacznik_przed_punktem() {
    TrackWriter writer;
    char header[128];
    char out[64];

    writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), header, sizeof(header));

    // Wyjazd z tunelu: 15 minut i cztery kilometry dalej.
    const size_t len =
        writer.append(pointAt(1957988, 5133612, 1757002134, 0, true), out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("-\n340,84,900,0\n", out);
    TEST_ASSERT_EQUAL_UINT32(15, len);
}

void test_czas_nieznany_idzie_jako_zero() {
    // Modul podaje czas UTC zanim zlapie fix pozycyjny, ale nie zawsze —
    // slad bez znanego czasu nadal ma sens jako ksztalt, wiec t0=0 jest
    // stanem normalnym, nie bledem.
    TrackWriter writer;
    char out[128];

    writer.begin(testHeader(), pointAt(1957648, 5133528, 0), out, sizeof(out));

    TEST_ASSERT_NOT_NULL(std::strstr(out, "t0=0\n"));
}

// ── Odtworzenie ────────────────────────────────────────────────────────────

void test_delty_odtwarzaja_slad_co_do_jednostki() {
    // Sedno wyboru liczb calkowitych: po tysiacach punktow ostatnia
    // wspolrzedna ma byc DOKLADNIE ta, ktora wypuscil decymator.
    TrackWriter writer;
    char header[128];
    char line[64];
    std::string file;

    std::vector<Point> emitted;
    for (int i = 0; i < 5000; ++i) {
        // Trasa krecaca sie w obie strony, zeby delty mialy oba znaki.
        const int32_t lon = 1957648 + i * 37 - (i % 7) * 11;
        const int32_t lat = 5133528 - i * 23 + (i % 5) * 17;
        emitted.push_back(pointAt(lon, lat, 1757001234 + static_cast<uint32_t>(i),
                                  static_cast<int8_t>((i % 90) - 45)));
    }

    writer.begin(testHeader(), emitted[0], header, sizeof(header));
    for (size_t i = 1; i < emitted.size(); ++i) {
        const size_t len = writer.append(emitted[i], line, sizeof(line));
        TEST_ASSERT_TRUE_MESSAGE(len > 0, "linia punktu sie nie zmiescila");
        file += line;
    }

    // Odtworzenie tak, jak zrobi to strona: suma delt od punktu startowego.
    int32_t lon = emitted[0].lonE5;
    int32_t lat = emitted[0].latE5;
    uint32_t timeS = emitted[0].timeS;
    size_t index = 1;

    size_t pos = 0;
    while (pos < file.size()) {
        const size_t end = file.find('\n', pos);
        const std::string row = file.substr(pos, end - pos);
        pos = end + 1;
        if (row == "-") continue;

        long dlon = 0, dlat = 0, dt = 0;
        int lean = 0;
        TEST_ASSERT_EQUAL_INT(4, std::sscanf(row.c_str(), "%ld,%ld,%ld,%d", &dlon, &dlat, &dt,
                                             &lean));
        lon += static_cast<int32_t>(dlon);
        lat += static_cast<int32_t>(dlat);
        timeS += static_cast<uint32_t>(dt);

        TEST_ASSERT_EQUAL_INT32(emitted[index].lonE5, lon);
        TEST_ASSERT_EQUAL_INT32(emitted[index].latE5, lat);
        TEST_ASSERT_EQUAL_UINT32(emitted[index].timeS, timeS);
        TEST_ASSERT_EQUAL_INT(emitted[index].leanDeg, lean);
        ++index;
    }

    TEST_ASSERT_EQUAL_UINT32(emitted.size(), index);
}

void test_decymator_i_format_wspolpracuja() {
    // Sprawdzenie, ze punkty z decymatora przechodza przez zapis bez utraty
    // czegokolwiek — lacznie ze znacznikami segmentow.
    TrackDecimator dec;
    dec.reset();

    TrackWriter writer;
    char header[128];
    char line[64];
    std::string file;
    Point point;

    bool first = true;
    for (uint32_t i = 0; i < 40; ++i) {
        // Luk, potem przerwa, potem dalszy ciag.
        Fix fix;
        fix.lonE5 = 1957648 + static_cast<int32_t>(i) * 40;
        fix.latE5 = 5133528 + static_cast<int32_t>(i) * static_cast<int32_t>(i);
        fix.timeS = i;
        fix.leanDeg = static_cast<int8_t>(i);

        if (i == 20) {
            if (dec.flush(point)) {
                writer.append(point, line, sizeof(line));
                file += line;
            }
            dec.breakSegment();
        }

        if (dec.update(fix, point)) {
            if (first) {
                writer.begin(testHeader(), point, header, sizeof(header));
                first = false;
                file += header;
            } else {
                writer.append(point, line, sizeof(line));
                file += line;
            }
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(file.find("\n-\n") != std::string::npos,
                             "znacznik przerwy nie trafil do pliku");
    TEST_ASSERT_EQUAL_UINT32(0, file.find("MMBT1\n"));
}

// ── Odmowy ─────────────────────────────────────────────────────────────────

void test_za_maly_bufor_nie_pisze_polowy_naglowka() {
    // Obciety naglowek jest gorszy niz jego brak: plik wyglada na poprawny.
    TrackWriter writer;
    char out[20];

    const size_t len =
        writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT32(0, len);
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_FALSE(writer.started());
}

void test_za_maly_bufor_nie_pisze_polowy_punktu() {
    TrackWriter writer;
    char header[128];
    writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), header, sizeof(header));

    char out[6];
    const size_t len = writer.append(pointAt(1957761, 5133346, 1757001239, 12), out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT32(0, len);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_punkt_bez_naglowka_jest_odrzucany() {
    // Bez punktu startowego delta liczylaby sie wzgledem zera, czyli slad
    // ladowalby w Zatoce Gwinejskiej.
    TrackWriter writer;
    char out[64];

    const size_t len = writer.append(pointAt(1957761, 5133346, 1757001239, 12), out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT32(0, len);
}

void test_reset_wymaga_ponownego_naglowka() {
    TrackWriter writer;
    char out[128];

    writer.begin(testHeader(), pointAt(1957648, 5133528, 1757001234), out, sizeof(out));
    writer.reset();

    TEST_ASSERT_FALSE(writer.started());
    TEST_ASSERT_EQUAL_UINT32(0, writer.append(pointAt(1957761, 5133346, 1757001239), out,
                                              sizeof(out)));
}


// ── Wznowienie po restarcie ────────────────────────────────────────────────

namespace {

/// Podaje caly plik skanerowi, linia po linii.
/// @return liczba linii przyjetych, zanim ktoras zostala odrzucona.
size_t scanAll(TrackScanner& scanner, const std::string& file) {
    size_t accepted = 0;
    size_t pos = 0;
    while (pos < file.size()) {
        const size_t end = file.find('\n', pos);
        const bool complete = end != std::string::npos;
        const std::string row = file.substr(pos, complete ? end - pos : std::string::npos);

        // Linia bez zakonczenia to przerwany zapis — nie podajemy jej dalej.
        if (!complete) break;
        if (!scanner.feedLine(row.c_str())) break;

        ++accepted;
        pos = end + 1;
    }
    return accepted;
}

std::string sampleFile() {
    return "MMBT1\n"
           "dev=70041ddc6bc8\n"
           "fw=1.0.0\n"
           "eps=8\n"
           "t0=1757001234\n"
           "p0=1957648,5133528\n"
           "\n"
           "113,-182,5,12\n"
           "26,-3,1,-31\n"
           "-\n"
           "340,84,900,0\n";
}

}  // namespace

void test_skaner_odtwarza_ostatni_punkt() {
    TrackScanner scanner;
    scanner.reset();
    scanAll(scanner, sampleFile());

    TEST_ASSERT_TRUE(scanner.ready());
    TEST_ASSERT_TRUE(scanner.timed());
    TEST_ASSERT_EQUAL_UINT32(3, scanner.points());
    TEST_ASSERT_EQUAL_INT32(1958127, scanner.last().lonE5);
    TEST_ASSERT_EQUAL_INT32(5133427, scanner.last().latE5);
    TEST_ASSERT_EQUAL_UINT32(1757002140, scanner.last().timeS);
}

void test_urwany_zapis_nie_uniewaznia_calego_pliku() {
    // Zanik zasilania w trakcie zapisu tnie OSTATNIA linie. Wszystko przed nia
    // jest dobre i ma zostac — kasowanie calosci kosztowaloby cala trase,
    // czyli dokladnie to, przed czym mial chronic zapis na flash.
    std::string file = sampleFile();
    file += "340,84";  // linia bez konca i bez nowej linii

    TrackScanner scanner;
    scanner.reset();
    const size_t accepted = scanAll(scanner, file);

    TEST_ASSERT_TRUE(scanner.ready());
    TEST_ASSERT_EQUAL_UINT32(3, scanner.points());
    TEST_ASSERT_EQUAL_INT32(1958127, scanner.last().lonE5);
    // Wszystkie kompletne linie przyjete: magia + 5 naglowka + pusta + 4 punkty.
    TEST_ASSERT_EQUAL_UINT32(11, accepted);
}

void test_uszkodzona_linia_konczy_skanowanie() {
    std::string file = sampleFile();
    file += "340,84,to-nie-liczba,0\n";
    file += "10,10,1,0\n";

    TrackScanner scanner;
    scanner.reset();
    scanAll(scanner, file);

    // Punkt po uszkodzonej linii NIE moze zostac doliczony: delty licza sie
    // po kolei, wiec za dziura wszystko jest przesuniete.
    TEST_ASSERT_EQUAL_UINT32(3, scanner.points());
}

void test_slad_bez_czasu_jest_rozpoznany() {
    std::string file = sampleFile();
    file.replace(file.find("t0=1757001234"), std::strlen("t0=1757001234"), "t0=0");

    TrackScanner scanner;
    scanner.reset();
    scanAll(scanner, file);

    TEST_ASSERT_TRUE(scanner.ready());
    TEST_ASSERT_FALSE_MESSAGE(scanner.timed(), "t0=0 znaczy slad bez czasu");
}

void test_plik_bez_magii_jest_odrzucany() {
    TrackScanner scanner;
    scanner.reset();
    TEST_ASSERT_FALSE(scanner.feedLine("MMBT9"));
    TEST_ASSERT_FALSE(scanner.ready());
}

void test_naglowek_bez_punktu_startowego_jest_bezuzyteczny() {
    TrackScanner scanner;
    scanner.reset();
    TEST_ASSERT_TRUE(scanner.feedLine("MMBT1"));
    TEST_ASSERT_TRUE(scanner.feedLine("dev=70041ddc6bc8"));
    TEST_ASSERT_TRUE(scanner.feedLine("t0=1757001234"));
    TEST_ASSERT_FALSE_MESSAGE(scanner.feedLine(""), "bez p0 nie ma od czego liczyc delt");
    TEST_ASSERT_FALSE(scanner.ready());
}

void test_wznowienie_pisze_delty_wzgledem_odtworzonego_punktu() {
    // Calosc mechanizmu: skaner odtwarza stan, writer pisze dalej do tego
    // samego pliku, a punkt po restarcie zaczyna nowy segment.
    TrackScanner scanner;
    scanner.reset();
    scanAll(scanner, sampleFile());

    TrackWriter writer;
    writer.resume(scanner.last());
    TEST_ASSERT_TRUE(writer.started());

    char out[64];
    Point next = pointAt(1958240, 5133245, 1757002145, 7, true);
    const size_t length = writer.append(next, out, sizeof(out));

    TEST_ASSERT_TRUE(length > 0);
    TEST_ASSERT_EQUAL_STRING("-\n113,-182,5,7\n", out);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_naglowek_znak_w_znak);
    RUN_TEST(test_punkt_jako_delta_znak_w_znak);
    RUN_TEST(test_przechyl_w_lewo_idzie_ze_znakiem);
    RUN_TEST(test_przerwa_dopisuje_znacznik_przed_punktem);
    RUN_TEST(test_czas_nieznany_idzie_jako_zero);
    RUN_TEST(test_delty_odtwarzaja_slad_co_do_jednostki);
    RUN_TEST(test_decymator_i_format_wspolpracuja);
    RUN_TEST(test_za_maly_bufor_nie_pisze_polowy_naglowka);
    RUN_TEST(test_za_maly_bufor_nie_pisze_polowy_punktu);
    RUN_TEST(test_punkt_bez_naglowka_jest_odrzucany);
    RUN_TEST(test_reset_wymaga_ponownego_naglowka);
    RUN_TEST(test_skaner_odtwarza_ostatni_punkt);
    RUN_TEST(test_urwany_zapis_nie_uniewaznia_calego_pliku);
    RUN_TEST(test_uszkodzona_linia_konczy_skanowanie);
    RUN_TEST(test_slad_bez_czasu_jest_rozpoznany);
    RUN_TEST(test_plik_bez_magii_jest_odrzucany);
    RUN_TEST(test_naglowek_bez_punktu_startowego_jest_bezuzyteczny);
    RUN_TEST(test_wznowienie_pisze_delty_wzgledem_odtworzonego_punktu);
    return UNITY_END();
}
