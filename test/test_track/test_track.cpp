// Motusy Moto Box — testy decymatora sladu trasy (srodowisko native).
//
// Uruchomienie:  pio test -e native
//
// Najwazniejszy test to `test_kazdy_odrzucony_fix_lezy_w_korytarzu` — pilnuje
// jedynej wlasnosci, dla ktorej korytarz zostal wybrany zamiast progow na
// predkosc i zakret: slad nigdzie nie odbiega od trasy wiecej niz epsilon.
// Reszta testow opisuje zachowania, ktore latwo zepsuc przy optymalizacji.

#include <unity.h>

#include <cmath>
#include <vector>

#include "TrackDecimator.h"

using namespace track;

namespace {

// Punkt odniesienia gdzies w Polsce — kolejnosc lon, lat jak w kontrakcie.
constexpr int32_t kLon0 = 1957000;
constexpr int32_t kLat0 = 5213000;

constexpr float kPi = 3.14159265f;
constexpr float kMetersPerUnitLat = 1.1132f;

float metersPerUnitLon() {
    return kMetersPerUnitLat * std::cos(52.13f * kPi / 180.0f);
}

/// Metry wzgledem punktu odniesienia -> jednostki 1e-5 stopnia.
Fix fixAt(float xM, float yM, uint32_t timeS, int8_t leanDeg = 0) {
    Fix fix;
    fix.lonE5 = kLon0 + static_cast<int32_t>(std::lround(xM / metersPerUnitLon()));
    fix.latE5 = kLat0 + static_cast<int32_t>(std::lround(yM / kMetersPerUnitLat));
    fix.timeS = timeS;
    fix.leanDeg = leanDeg;
    return fix;
}

struct MetricPoint {
    float x;
    float y;
};

MetricPoint toMeters(int32_t lonE5, int32_t latE5) {
    return {static_cast<float>(lonE5 - kLon0) * metersPerUnitLon(),
            static_cast<float>(latE5 - kLat0) * kMetersPerUnitLat};
}

float distanceToSegment(MetricPoint p, MetricPoint a, MetricPoint b) {
    const float bx = b.x - a.x;
    const float by = b.y - a.y;
    const float ax = p.x - a.x;
    const float ay = p.y - a.y;

    const float len2 = bx * bx + by * by;
    if (len2 <= 0.0f) return std::sqrt(ax * ax + ay * ay);

    float t = (ax * bx + ay * by) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const float dx = ax - t * bx;
    const float dy = ay - t * by;
    return std::sqrt(dx * dx + dy * dy);
}

/// Najmniejsza odleglosc fixu od lamanej zlozonej z zapisanych punktow.
float distanceToTrack(const Fix& fix, const std::vector<Point>& track) {
    const MetricPoint p = toMeters(fix.lonE5, fix.latE5);
    float best = 1e9f;
    for (size_t i = 1; i < track.size(); ++i) {
        const MetricPoint a = toMeters(track[i - 1].lonE5, track[i - 1].latE5);
        const MetricPoint b = toMeters(track[i].lonE5, track[i].latE5);
        const float d = distanceToSegment(p, a, b);
        if (d < best) best = d;
    }
    return best;
}

/// Przepuszcza cala trase przez decymator i domyka ja `flush()`.
std::vector<Point> run(TrackDecimator& dec, const std::vector<Fix>& route) {
    std::vector<Point> out;
    Point point;
    for (const Fix& fix : route) {
        if (dec.update(fix, point)) out.push_back(point);
    }
    if (dec.flush(point)) out.push_back(point);
    return out;
}

/// Prosta na wschod: `count` fixow co sekunde, `speedMs` metrow na sekunde.
std::vector<Fix> straightRoute(size_t count, float speedMs) {
    std::vector<Fix> route;
    for (size_t i = 0; i < count; ++i) {
        route.push_back(fixAt(speedMs * static_cast<float>(i), 0.0f, static_cast<uint32_t>(i)));
    }
    return route;
}

/// Luk o zadanym promieniu, brany z zadana predkoscia, probkowany 1 Hz.
/// Zaczyna sie w (0,0) i skreca w lewo.
std::vector<Fix> arcRoute(float radiusM, float speedMs, float sweepRad, uint32_t startS,
                          int8_t leanDeg = 0) {
    std::vector<Fix> route;
    const float omega = speedMs / radiusM;  // rad/s
    const auto steps = static_cast<size_t>(sweepRad / omega);
    for (size_t i = 0; i <= steps; ++i) {
        const float angle = omega * static_cast<float>(i);
        route.push_back(fixAt(radiusM * std::sin(angle), radiusM * (1.0f - std::cos(angle)),
                              startS + static_cast<uint32_t>(i), leanDeg));
    }
    return route;
}

}  // namespace

// ── Gwarancja korytarza ────────────────────────────────────────────────────

void test_kazdy_odrzucony_fix_lezy_w_korytarzu() {
    // Trasa mieszana: prosta, szeroki luk, ciasny nawrot, znowu prosta.
    std::vector<Fix> route = straightRoute(12, 25.0f);

    uint32_t t = 12;
    const float x0 = 25.0f * 11.0f;
    for (const Fix& fix : arcRoute(100.0f, 20.0f, kPi / 2.0f, t)) {
        Fix shifted = fix;
        shifted.lonE5 += static_cast<int32_t>(std::lround(x0 / metersPerUnitLon()));
        route.push_back(shifted);
        t = shifted.timeS;
    }
    for (const Fix& fix : arcRoute(20.0f, 8.0f, kPi, t + 1)) {
        Fix shifted = fix;
        shifted.lonE5 += static_cast<int32_t>(std::lround((x0 + 100.0f) / metersPerUnitLon()));
        shifted.latE5 += static_cast<int32_t>(std::lround(100.0f / kMetersPerUnitLat));
        route.push_back(shifted);
    }

    TrackDecimator dec;
    dec.reset();
    const std::vector<Point> track = run(dec, route);

    TEST_ASSERT_TRUE_MESSAGE(track.size() >= 2, "slad musi miec co najmniej odcinek");

    float worst = 0.0f;
    for (const Fix& fix : route) {
        const float d = distanceToTrack(fix, track);
        if (d > worst) worst = d;
    }
    // Epsilon 8 m; margines na arytmetyke zmiennoprzecinkowa, nie na algorytm.
    TEST_ASSERT_TRUE_MESSAGE(worst <= 8.05f, "slad odbiegl od trasy wiecej niz korytarz");
}

// ── Gdzie decymator wydaje punkty ──────────────────────────────────────────

void test_prosta_nie_kosztuje_prawie_nic() {
    // 50 sekund prostej przy 25 m/s to 1,25 km. Korytarz nie ma tu czego
    // zapisywac: caly odcinek opisuje jedna linia.
    TrackDecimator dec;
    dec.reset();
    const std::vector<Point> track = run(dec, straightRoute(50, 25.0f));

    // Punkt startowy + domkniecie przez flush(). Twardy ogranicznik 60 s
    // jeszcze nie zdazyl zadzialac.
    TEST_ASSERT_EQUAL_UINT32(2, track.size());
}

void test_zakret_dostaje_gesciej_niz_prosta() {
    // Sedno korytarza: przy TEJ SAMEJ liczbie fixow zakret ma dostac wiecej
    // punktow niz prosta. To jest cala roznica wobec stalego odstepu, ktory
    // wydaje tyle samo bajtow niezaleznie od tego, czy cos sie dzieje.
    //
    // NIE testujemy tu "punkt co sekunde w zakrecie" — pomiar na syntetycznych
    // trasach (2026-09-04, docs/gpx-slad-trasy.md §3) pokazal, ze przy szumie
    // GPS rzedu 2-3 m ciasny nawrot dostaje 3 punkty niezaleznie od epsilon.
    // To granica czujnika, nie algorytmu.
    const std::vector<Fix> corner = arcRoute(30.0f, 11.1f, kPi, 0);

    TrackDecimator cornerDec;
    cornerDec.reset();
    const std::vector<Point> cornerTrack = run(cornerDec, corner);

    TrackDecimator straightDec;
    straightDec.reset();
    const std::vector<Point> straightTrack = run(straightDec, straightRoute(corner.size(), 11.1f));

    TEST_ASSERT_TRUE_MESSAGE(cornerTrack.size() > straightTrack.size(),
                             "zakret nie dostal gesciej niz prosta tej samej dlugosci");

    float worst = 0.0f;
    for (const Fix& fix : corner) {
        const float d = distanceToTrack(fix, cornerTrack);
        if (d > worst) worst = d;
    }
    TEST_ASSERT_TRUE_MESSAGE(worst <= 8.05f, "zakret sciety poza korytarz");
}

void test_postoj_pisze_punkt_co_minute() {
    // Stanie w miejscu ma nie zapisywac 720 identycznych punktow na godzine,
    // ale tez nie milczec — inaczej postoj znika ze sladu bez sladu.
    TrackDecimator dec;
    dec.reset();

    std::vector<Fix> route;
    for (uint32_t i = 0; i < 180; ++i) route.push_back(fixAt(0.0f, 0.0f, i));

    const std::vector<Point> track = run(dec, route);
    // Trzy minuty postoju: start + mniej wiecej jeden punkt na minute.
    TEST_ASSERT_TRUE(track.size() >= 3);
    TEST_ASSERT_TRUE(track.size() <= 5);
}

void test_twardy_ogranicznik_szescdziesieciu_sekund() {
    TrackDecimator dec;
    dec.reset();
    const std::vector<Point> track = run(dec, straightRoute(130, 25.0f));

    // Dwie minuty prostej: punkt startowy, dwa z ogranicznika, domkniecie.
    TEST_ASSERT_TRUE(track.size() >= 3);
    for (size_t i = 1; i < track.size(); ++i) {
        TEST_ASSERT_TRUE(track[i].timeS - track[i - 1].timeS <= 60);
    }
}

// ── Przechyl ───────────────────────────────────────────────────────────────

void test_przechyl_punktu_to_maksimum_z_odcinka() {
    // Punkt zastepuje caly odcinek, wiec ma niesc najwiekszy przechyl z tego
    // odcinka — inaczej najciekawszy kat gubilby sie tam, gdzie decymator
    // dziala najmocniej, czyli w zakrecie.
    TrackDecimator dec;
    dec.reset();

    std::vector<Fix> route = arcRoute(20.0f, 8.0f, kPi, 0, 0);
    // Rosnacy przechyl w prawo, potem jeden mocny w lewo.
    for (size_t i = 0; i < route.size(); ++i) {
        route[i].leanDeg = static_cast<int8_t>(i * 3);
    }
    route[route.size() / 2].leanDeg = -47;

    const std::vector<Point> track = run(dec, route);

    bool sawLeft = false;
    for (const Point& point : track) {
        if (point.leanDeg == -47) sawLeft = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(sawLeft, "najwiekszy przechyl w lewo zniknal ze sladu");
}

void test_przechyl_zachowuje_strone_zakretu() {
    // Maksimum liczone co do wartosci bezwzglednej, ale ZE ZNAKIEM.
    TrackDecimator dec;
    dec.reset();

    std::vector<Fix> route = arcRoute(20.0f, 8.0f, kPi, 0, 0);
    for (size_t i = 0; i < route.size(); ++i) {
        route[i].leanDeg = static_cast<int8_t>(-40 + static_cast<int>(i));
    }

    const std::vector<Point> track = run(dec, route);

    bool anyNegative = false;
    for (size_t i = 1; i < track.size(); ++i) {
        if (track[i].leanDeg < 0) anyNegative = true;
    }
    TEST_ASSERT_TRUE_MESSAGE(anyNegative, "znak przechylu zgubiony");
}

// ── Segmenty ───────────────────────────────────────────────────────────────

void test_pierwszy_punkt_zaczyna_segment() {
    TrackDecimator dec;
    dec.reset();

    Point point;
    TEST_ASSERT_TRUE(dec.update(fixAt(0.0f, 0.0f, 0), point));
    TEST_ASSERT_TRUE(point.startsSegment);
}

void test_po_przerwie_zaczyna_sie_nowy_segment() {
    // Tunel, wiadukt, garaz: bez znacznika mapa narysowalaby prosta przez
    // pol miasta.
    TrackDecimator dec;
    dec.reset();

    Point point;
    dec.update(fixAt(0.0f, 0.0f, 0), point);
    dec.update(fixAt(25.0f, 0.0f, 1), point);
    dec.update(fixAt(50.0f, 0.0f, 2), point);

    // Domkniecie ogona, dopiero potem zgloszenie przerwy — ta para wywolan
    // jest calym mechanizmem segmentow.
    TEST_ASSERT_TRUE_MESSAGE(dec.flush(point), "ogon segmentu przepadl");
    TEST_ASSERT_FALSE(point.startsSegment);
    dec.breakSegment();

    TEST_ASSERT_TRUE(dec.update(fixAt(4000.0f, 0.0f, 300), point));
    TEST_ASSERT_TRUE_MESSAGE(point.startsSegment, "punkt po przerwie nie zaczal segmentu");
}

void test_flush_bez_bufora_nic_nie_oddaje() {
    TrackDecimator dec;
    dec.reset();

    Point point;
    dec.update(fixAt(0.0f, 0.0f, 0), point);
    // Sam punkt startowy jest juz zapisany, w buforze nie ma nic.
    TEST_ASSERT_FALSE(dec.flush(point));
}

void test_reset_zapomina_poprzedni_przejazd() {
    TrackDecimator dec;
    dec.reset();

    Point point;
    dec.update(fixAt(0.0f, 0.0f, 0), point);
    dec.update(fixAt(25.0f, 0.0f, 1), point);

    dec.reset();
    TEST_ASSERT_EQUAL_UINT32(0, dec.buffered());
    TEST_ASSERT_TRUE(dec.update(fixAt(9000.0f, 9000.0f, 0), point));
    TEST_ASSERT_TRUE_MESSAGE(point.startsSegment, "nowy przejazd musi zaczac segment");
}

// ── Konfiguracja ───────────────────────────────────────────────────────────

void test_szerszy_korytarz_daje_mniej_punktow() {
    // Wlasnosc, nie liczba: luzniejszy korytarz nie moze zapisac wiecej.
    const std::vector<Fix> route = arcRoute(60.0f, 15.0f, kPi, 0);

    TrackDecimatorConfig tight;
    tight.corridorM = 2.0f;
    TrackDecimator tightDec(tight);
    tightDec.reset();
    const size_t tightCount = run(tightDec, route).size();

    TrackDecimatorConfig wide;
    wide.corridorM = 20.0f;
    TrackDecimator wideDec(wide);
    wideDec.reset();
    const size_t wideCount = run(wideDec, route).size();

    TEST_ASSERT_TRUE_MESSAGE(wideCount <= tightCount, "szerszy korytarz zapisal wiecej punktow");
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_kazdy_odrzucony_fix_lezy_w_korytarzu);
    RUN_TEST(test_prosta_nie_kosztuje_prawie_nic);
    RUN_TEST(test_zakret_dostaje_gesciej_niz_prosta);
    RUN_TEST(test_postoj_pisze_punkt_co_minute);
    RUN_TEST(test_twardy_ogranicznik_szescdziesieciu_sekund);
    RUN_TEST(test_przechyl_punktu_to_maksimum_z_odcinka);
    RUN_TEST(test_przechyl_zachowuje_strone_zakretu);
    RUN_TEST(test_pierwszy_punkt_zaczyna_segment);
    RUN_TEST(test_po_przerwie_zaczyna_sie_nowy_segment);
    RUN_TEST(test_flush_bez_bufora_nic_nie_oddaje);
    RUN_TEST(test_reset_zapomina_poprzedni_przejazd);
    RUN_TEST(test_szerszy_korytarz_daje_mniej_punktow);
    return UNITY_END();
}
