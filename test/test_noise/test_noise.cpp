// Motusy Moto Box — testy toru pomiaru halasu (srodowisko native).
//
// Uruchomienie:  pio test -e native
//
// Trzy rodzaje testow, kazdy pilnuje czego innego:
//
//   1. PRZELICZNIK — charakterystyka filtru A wobec IEC 61672. To sa liczby
//      przepisane z pierwszego urzadzenia i nie wolno ich stroic; test istnieje
//      po to, zeby nikt tego nie zrobil przez przypadek.
//   2. METRYKA — czy L_sus faktycznie odrzuca piki i zachowuje warkot.
//      To jest cala racja bytu tej klasy, wiec tu jest najwiecej przypadkow.
//   3. POLITYKA — bramka predkosci, zapamietana predkosc, liczniki diagnostyczne.
//      Rzeczy specyficzne dla tego urzadzenia, ktorych w docs/noice.md nie ma.
//
// SYGNAL TESTOWY to sinus 1 kHz, bo tam wazenie A ma dokladnie 0 dB — poziom
// zadany na wejsciu jest wiec poziomem, ktory ma wyjsc na koncu toru.

#include <unity.h>

#include <cmath>
#include <cstddef>

#include "AWeighting.h"
#include "NoiseMeter.h"
#include "RideNoise.h"
#include "SustainedLevel.h"
#include "TimeWeighting.h"

using namespace noise;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kSampleRate = 48000.0f;
constexpr float kToneHz = 1000.0f;

/// Kalibracja uzywana w testach — ta sama, co w config.h, zeby liczby
/// w tescie byly tymi samymi liczbami, co na urzadzeniu.
constexpr float kCalibrationDb = 133.6f;

/// Bin histogramu ma 0,5 dB, wiec wynik jest kwantowany. Polowa kosza
/// z zapasem na zaokraglenie.
constexpr float kBinTolerance = 0.5f;

/// Amplituda sinusa dajaca zadany poziom w dB(A) na wyjsciu toru.
/// Dla sinusa srednia moc to A^2/2, stad pierwiastek z dwoch.
double amplitudeFor(double levelDbA) {
    const double dbfs = levelDbA - kCalibrationDb;
    return std::sqrt(2.0 * std::pow(10.0, dbfs / 10.0));
}

/// Generator z ciagla faza. Ciaglosc jest istotna: skok fazy przy kazdej
/// zmianie poziomu bylby trzaskiem, czyli dokladnie tym, co ta metryka
/// ma odrzucac — test mierzylby wtedy artefakt wlasnego stanowiska.
class Tone {
public:
    void feed(NoiseMeter& meter, double levelDbA, double seconds) {
        const double amplitude = amplitudeFor(levelDbA);
        const double step = 2.0 * kPi * kToneHz / kSampleRate;
        const size_t count = static_cast<size_t>(kSampleRate * seconds + 0.5);
        for (size_t i = 0; i < count; ++i) {
            meter.addSample(static_cast<float>(amplitude * std::sin(phase_)));
            phase_ += step;
            if (phase_ > 2.0 * kPi) phase_ -= 2.0 * kPi;
        }
    }

private:
    double phase_ = 0.0;
};

NoiseMeterConfig testConfig(float windowSec = 5.0f) {
    NoiseMeterConfig config;
    config.sampleRateHz = kSampleRate;
    config.windowSec = windowSec;
    config.calibrationDb = kCalibrationDb;
    return config;
}

/// Odpowiedz filtru A zmierzona na sinusie: RMS wyjscia wobec RMS wejscia.
double weightingDb(double hz) {
    AWeighting weighting(kSampleRate);
    const double amplitude = 0.1;
    const double step = 2.0 * kPi * hz / kSampleRate;
    const size_t settle = static_cast<size_t>(kSampleRate);
    const size_t measure = static_cast<size_t>(kSampleRate);

    double phase = 0.0;
    for (size_t i = 0; i < settle; ++i) {
        weighting.process(static_cast<float>(amplitude * std::sin(phase)));
        phase += step;
    }

    double sum = 0.0;
    for (size_t i = 0; i < measure; ++i) {
        const float y = weighting.process(static_cast<float>(amplitude * std::sin(phase)));
        phase += step;
        sum += static_cast<double>(y) * y;
    }

    const double rmsOut = std::sqrt(sum / measure);
    const double rmsIn = amplitude / std::sqrt(2.0);
    return 20.0 * std::log10(rmsOut / rmsIn);
}

}  // namespace

// ── 1. Przelicznik ──────────────────────────────────────────────────────────

void test_wazenie_a_trafia_w_norme() {
    // Wartosci kontrolne z IEC 61672-1. Jesli ktorykolwiek z tych testow
    // zaczyna padac, ktos ruszyl czestotliwosci narozne albo normalizacje —
    // a to sa PRZELICZNIKI, wspolne z DBMeterV1, nie parametry do strojenia.
    TEST_ASSERT_FLOAT_WITHIN(0.1, -19.1, weightingDb(100.0));
    TEST_ASSERT_FLOAT_WITHIN(0.1, -3.2, weightingDb(500.0));
    TEST_ASSERT_FLOAT_WITHIN(0.1, 0.0, weightingDb(1000.0));
    TEST_ASSERT_FLOAT_WITHIN(0.1, 1.2, weightingDb(2000.0));
}

void test_wazenie_a_przy_8k_odbiega_o_znane_pol_decybela() {
    // Norma mowi -1,1 dB, a nam wychodzi -1,69. Ta roznica NIE jest bledem:
    // transformacja biliniowa deformuje charakterystyke tym mocniej, im blizej
    // Nyquista, i przy 48 kHz daje wlasnie ok. 0,6 dB na 8 kHz (przy 32 kHz
    // byloby 1,5 dB — stad wybor 48 kHz).
    //
    // Test przypina te wartosc, zeby przypadkowe wlaczenie prewarpingu albo
    // zmiana czestotliwosci probkowania nie przeszly niezauwazone.
    TEST_ASSERT_FLOAT_WITHIN(0.1, -1.69, weightingDb(8000.0));
}

void test_usredniamy_moc_a_nie_decybele() {
    // Sygnal skaczacy miedzy -80 a -40 dBFS. Poprawnie usredniona moc daje
    // 10*log10((1e-8 + 1e-4)/2) = -43,0 dB. Usrednianie decybeli daloby -60,0.
    // Siedemnascie decybeli roznicy — motocykl na przepustnicy to dokladnie
    // taki sygnal.
    TimeWeighting fast(0.125f, kSampleRate);
    const float quiet = static_cast<float>(std::pow(10.0, -80.0 / 20.0));
    const float loud = static_cast<float>(std::pow(10.0, -40.0 / 20.0));

    for (size_t i = 0; i < 480000; ++i) fast.process((i % 2) ? loud : quiet);

    TEST_ASSERT_FLOAT_WITHIN(0.1, -43.0, TimeWeighting::toDb(fast.meanSquare()));
}

void test_fast_opada_34_decybele_na_sekunde() {
    // Wlasnosc normy, nie usterka — ale trzeba ja znac, czytajac wyniki:
    // pik 70 dB ponad tlem wraca do tla dopiero po dwoch sekundach. To dlatego
    // seria pikow co pol sekundy JEST dla miernika halasem ciaglym.
    TimeWeighting fast(0.125f, kSampleRate);
    for (size_t i = 0; i < 48000; ++i) fast.process(0.5f);
    const float before = TimeWeighting::toDb(fast.meanSquare());

    for (size_t i = 0; i < 48000; ++i) fast.process(0.0f);
    const float after = TimeWeighting::toDb(fast.meanSquare());

    TEST_ASSERT_FLOAT_WITHIN(0.5, 34.7, before - after);
}

// ── 2. Metryka ──────────────────────────────────────────────────────────────

void test_pik_jest_odrzucany() {
    // Pik 120 dB przez 100 ms na tle 50 dB. Usrednianie po 5 s zostawiloby
    // z niego 103 dB. Erozja odrzuca go JAKOSCIOWO — wynik ma byc tlem.
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 50.0, 6.0);
    tone.feed(meter, 120.0, 0.1);
    tone.feed(meter, 50.0, 6.0);

    TEST_ASSERT_FLOAT_WITHIN(1.0, 50.0, meter.maxNoiseDb());
}

void test_warkot_jest_zachowany() {
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 85.0, 6.0);

    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 85.0, meter.maxNoiseDb());
}

void test_za_krotki_nie_liczy_sie() {
    // Trzy sekundy to za malo. Kazde okno 5 s zawiera co najmniej 2 s tla,
    // czyli 40 % — a pytamy o poziom utrzymany przez 90 % czasu.
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 50.0, 6.0);
    tone.feed(meter, 85.0, 3.0);
    tone.feed(meter, 50.0, 6.0);

    TEST_ASSERT_FLOAT_WITHIN(1.0, 50.0, meter.maxNoiseDb());
}

void test_dokladnie_piec_sekund() {
    // NAJWAZNIEJSZY TEST W TYM PLIKU. Filtr Fast potrzebuje ~0,28 s na
    // narastanie, wiec dzwiek trwajacy dokladnie 5,0 s daje pelny poziom
    // tylko przez ~4,7 s. Czyste minimum (tolerancja 100 %) odrzuciloby go
    // i metryka ">= 5 s" nie wykrylaby zdarzenia trwajacego 5 s.
    //
    // Ten test lapie regresje, gdyby ktos "uproscil" tolerancje z powrotem
    // do minimum.
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 50.0, 6.0);
    tone.feed(meter, 85.0, 5.0);
    tone.feed(meter, 50.0, 6.0);

    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 85.0, meter.maxNoiseDb());
}

void test_czyste_minimum_odrzuciloby_piec_sekund() {
    // Kontrdowod do testu wyzej: ta sama piecio-sekundowa probka przy
    // tolerancji 100 % daje tlo zamiast poziomu. Gdyby kiedys ktos uznal,
    // ze 90 % to "niepotrzebna komplikacja", ten test pokazuje koszt.
    NoiseMeterConfig config = testConfig();
    config.tolerancePercent = 100.0f;
    NoiseMeter meter(config);
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 50.0, 6.0);
    tone.feed(meter, 85.0, 5.0);
    tone.feed(meter, 50.0, 6.0);

    TEST_ASSERT_TRUE(meter.maxNoiseDb() < 84.0f);
}

void test_seria_pikow_to_nie_warkot() {
    // Piec pikow 105 dB po 100 ms, co 2,5 s.
    //
    // ODSTEP JEST DOBRANY, NIE PRZYPADKOWY. Dokument zrodlowy proponowal piki
    // co 0,5 s, ale przy opadaniu 34,7 dB/s poziom nie zdazy wtedy wrocic do
    // tla i miernik widzi halas CIAGLY — i sluszznie, bo impulsy dwa razy na
    // sekunde to dla ucha tez halas ciagly. Przy 2,5 s poziom wraca do tla
    // miedzy pikami i metryka ma je odrzucic.
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 50.0, 6.0);
    for (int i = 0; i < 5; ++i) {
        tone.feed(meter, 105.0, 0.1);
        tone.feed(meter, 50.0, 2.4);
    }

    TEST_ASSERT_FLOAT_WITHIN(1.0, 50.0, meter.maxNoiseDb());
}

void test_monotonicznosc() {
    // L_sus(5 s) <= L_sus(1 s). Zawsze — krotsze okno latwiej wypelnic
    // halasem. To wlasnosc, dzieki ktorej nie da sie zarzucic, ze metryka
    // zawyza: nigdy nie wyjdzie wyzej niz krotsze okno ani niz maksimum.
    NoiseMeter long5(testConfig(5.0f));
    NoiseMeter short1(testConfig(1.0f));
    long5.setRecording(true);
    short1.setRecording(true);

    Tone toneA;
    Tone toneB;
    const double levels[] = {50.0, 95.0, 60.0, 88.0, 105.0, 55.0, 92.0};
    const double spans[] = {6.0, 0.4, 1.5, 6.0, 0.2, 3.0, 2.0};

    for (size_t i = 0; i < 7; ++i) {
        toneA.feed(long5, levels[i], spans[i]);
        toneB.feed(short1, levels[i], spans[i]);
    }

    TEST_ASSERT_TRUE(long5.maxNoiseDb() <= short1.maxNoiseDb() + kBinTolerance);
}

// ── 3. Polityka ─────────────────────────────────────────────────────────────

void test_niepelne_okno_nic_nie_orzeka() {
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 85.0, 2.0);

    TEST_ASSERT_FALSE(meter.ready());
    TEST_ASSERT_EQUAL_FLOAT(NoiseMeter::kNoData, meter.maxNoiseDb());
}

void test_zamknieta_bramka_nie_ustanawia_rekordu() {
    // Krecenie gazem na postoju nie jest przejazdem. Ta sama regula, co dla
    // przechylu i przyspieszenia (architektura §16) — JEDNA BRAMKA dla
    // wszystkich pomiarow.
    NoiseMeter meter(testConfig());
    meter.setRecording(false);
    Tone tone;

    tone.feed(meter, 100.0, 8.0);
    TEST_ASSERT_EQUAL_FLOAT(NoiseMeter::kNoData, meter.maxNoiseDb());

    meter.setRecording(true);
    tone.feed(meter, 85.0, 6.0);
    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 85.0, meter.maxNoiseDb());
}

void test_krecenie_gazem_na_swiatlach_nie_wchodzi_do_rekordu() {
    // NAJWAZNIEJSZY TEST BRAMKI. Okno jest PRZESUWNE, wiec samo "bramka
    // otwarta" nie wystarcza: zaraz po jej otwarciu okno niesie jeszcze piec
    // sekund dzwieku sprzed niej. Bez osobnego zabezpieczenia blip gazem na
    // czerwonym swietle ustanawialby rekord calego przejazdu — czyli dokladnie
    // to, czemu bramka miala zapobiec.
    //
    // Ten przypadek nie jest hipotetyczny: to najzwyklejsza rzecz, jaka robi
    // sie na motocyklu na postoju.
    NoiseMeter meter(testConfig());
    Tone tone;

    meter.setRecording(true);
    tone.feed(meter, 85.0, 6.0);

    meter.setRecording(false);
    tone.feed(meter, 110.0, 3.0);  // swiatla, kilka blipow

    meter.setRecording(true);
    tone.feed(meter, 85.0, 6.0);

    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 85.0, meter.maxNoiseDb());
}

void test_cisza_przed_bramka_tez_nie_psuje_wyniku() {
    // Druga strona tej samej reguly: cisza z postoju nie moze zanizyc
    // pierwszego zmierzonego okna. Wychodzi to samo zabezpieczenie —
    // liczy sie dopiero okno pochodzace w calosci z jazdy.
    NoiseMeter meter(testConfig());
    Tone tone;

    meter.setRecording(false);
    tone.feed(meter, 40.0, 6.0);
    meter.setRecording(true);
    tone.feed(meter, 85.0, 6.0);

    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 85.0, meter.maxNoiseDb());
}

void test_predkosc_zapamietana_w_chwili_rekordu() {
    // To pole rozstrzyga pozniej, czy slychac wydech czy wiatr: jesli
    // noise_at_speed prawie zawsze rowna sie predkosci maksymalnej przejazdu,
    // mierzymy powietrze. Patrz docs/pomiar-halasu.md §6.
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    meter.setSpeedKmh(62.0f);
    tone.feed(meter, 90.0, 6.0);
    TEST_ASSERT_FLOAT_WITHIN(0.1, 62.0, meter.maxNoiseSpeedKmh());

    // Cichszy fragment przy innej predkosci nie rusza ani rekordu, ani
    // zapamietanej predkosci.
    meter.setSpeedKmh(148.0f);
    tone.feed(meter, 70.0, 6.0);
    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 90.0, meter.maxNoiseDb());
    TEST_ASSERT_FLOAT_WITHIN(0.1, 62.0, meter.maxNoiseSpeedKmh());

    // Glosniejszy przepisuje oba.
    meter.setSpeedKmh(95.0f);
    tone.feed(meter, 101.0, 6.0);
    TEST_ASSERT_FLOAT_WITHIN(kBinTolerance, 101.0, meter.maxNoiseDb());
    TEST_ASSERT_FLOAT_WITHIN(0.1, 95.0, meter.maxNoiseSpeedKmh());
}

void test_przesterowanie_jest_liczone() {
    // Miernik, ktory cicho obcina, KLAMIE — i to w dol. Bez tego licznika
    // rekord sezonu bylby po prostu najglosniejszym obcieciem.
    NoiseMeter meter(testConfig());
    meter.setRecording(true);

    Tone tone;
    tone.feed(meter, 60.0, 0.5);
    TEST_ASSERT_EQUAL_UINT32(0, meter.clippedCount());

    for (size_t i = 0; i < 100; ++i) meter.addSample((i % 2) ? 1.0f : -1.0f);
    TEST_ASSERT_EQUAL_UINT32(100, meter.clippedCount());
}

void test_zgubione_bloki_sa_liczone() {
    // Przepelnienie DMA nie zglasza sie bledem, tylko po cichu gubi probki.
    // Zgubionych nie da sie odtworzyc, ale da sie o nich powiedziec.
    NoiseMeter meter(testConfig());
    meter.addDropped(3);
    meter.addDropped(1);
    TEST_ASSERT_EQUAL_UINT32(4, meter.droppedCount());
}

void test_reset_zaczyna_przejazd_od_zera() {
    NoiseMeter meter(testConfig());
    meter.setRecording(true);
    Tone tone;

    tone.feed(meter, 95.0, 6.0);
    meter.addDropped(2);
    TEST_ASSERT_TRUE(meter.maxNoiseDb() > 90.0f);

    meter.reset();
    TEST_ASSERT_EQUAL_FLOAT(NoiseMeter::kNoData, meter.maxNoiseDb());
    TEST_ASSERT_EQUAL_UINT32(0, meter.clippedCount());
    TEST_ASSERT_EQUAL_UINT32(0, meter.droppedCount());
    TEST_ASSERT_FALSE(meter.ready());
}

void test_okno_krotsze_niz_domyslne_ma_wlasna_pojemnosc() {
    // windowSec jest parametrem, nie stala — gdyby 5 s okazalo sie zlym
    // wyborem, zmiana ma kosztowac jedna liczbe.
    SustainedLevel window(1.0f, 10.0f, 90.0f);
    TEST_ASSERT_EQUAL_UINT32(100, window.capacity());

    SustainedLevel standard(5.0f, 10.0f, 90.0f);
    TEST_ASSERT_EQUAL_UINT32(500, standard.capacity());
}

// ── 4. Rekord przejazdu ─────────────────────────────────────────────────────

void test_pakowanie_jest_odwracalne() {
    // Format na flashu jest jawny, bajt po bajcie — zrzut struktury wiazalby
    // go z ukladem pol w pamieci i nastepna zmiana czytalaby smieci.
    RideNoise value = makeRideNoise(108.35f, 62.0f, 12, 480, 3);
    uint8_t buffer[RideNoise::kPackedBytes];
    packRideNoise(value, buffer);

    RideNoise back;
    unpackRideNoise(buffer, back);

    TEST_ASSERT_EQUAL_INT16(value.maxDb10, back.maxDb10);
    TEST_ASSERT_EQUAL_UINT16(62, back.atSpeedKmh);
    TEST_ASSERT_EQUAL_UINT16(12, back.clipped);
    TEST_ASSERT_EQUAL_UINT16(480, back.dropped);
    TEST_ASSERT_EQUAL_UINT8(3, back.calibration);
}

void test_brak_pomiaru_przezywa_zapis() {
    // Cichy przejazd i przejazd bez mikrofonu to dwie rozne rzeczy. Gdyby brak
    // pomiaru zapisal sie jako zero, martwy mikrofon udawalby cisze.
    RideNoise value;
    uint8_t buffer[RideNoise::kPackedBytes];
    packRideNoise(value, buffer);

    RideNoise back = makeRideNoise(90.0f, 50.0f, 0, 0, 1);
    unpackRideNoise(buffer, back);

    TEST_ASSERT_FALSE(back.measured());
}

void test_poziom_ujemny_przezywa_zapis() {
    // Poziom moze byc ujemny (cisza ponizej progu kalibracji), a int16
    // pakowany po bajcie latwo tu zgubic znak.
    RideNoise value = makeRideNoise(-12.4f, 0.0f, 0, 0, 1);
    uint8_t buffer[RideNoise::kPackedBytes];
    packRideNoise(value, buffer);

    RideNoise back;
    unpackRideNoise(buffer, back);

    TEST_ASSERT_TRUE(back.measured());
    TEST_ASSERT_EQUAL_INT16(-124, back.maxDb10);
}

void test_liczniki_sie_nasycaja() {
    // Przewiniecie licznika pokazaloby male przesterowanie tam, gdzie bylo
    // ogromne — czyli klamalo w najgorsza strone.
    const RideNoise value = makeRideNoise(100.0f, 10.0f, 70000, 999999, 1);
    TEST_ASSERT_EQUAL_UINT16(65535, value.clipped);
    TEST_ASSERT_EQUAL_UINT16(65535, value.dropped);
}

void test_rekord_bierze_predkosc_razem_z_poziomem() {
    // Para (ile, przy jakiej predkosci) ma opisywac JEDNA chwile. Gdyby
    // predkosc aktualizowala sie osobno, zestawienie jej z predkoscia
    // maksymalna przejazdu nie mowiloby juz nic o wietrze.
    RideNoise record = makeRideNoise(95.0f, 60.0f, 0, 0, 1);

    // Cichszy fragment przy innej predkosci nie rusza niczego.
    record.raiseTo(makeRideNoise(80.0f, 150.0f, 0, 0, 1));
    TEST_ASSERT_EQUAL_INT16(950, record.maxDb10);
    TEST_ASSERT_EQUAL_UINT16(60, record.atSpeedKmh);

    // Glosniejszy przepisuje oba naraz.
    record.raiseTo(makeRideNoise(102.0f, 88.0f, 0, 0, 1));
    TEST_ASSERT_EQUAL_INT16(1020, record.maxDb10);
    TEST_ASSERT_EQUAL_UINT16(88, record.atSpeedKmh);
}

void test_restart_w_trakcie_jazdy_nie_kasuje_rekordu() {
    // Mikrofon zyje tylko przy wlaczonej stacyjce i po restarcie zaczyna od
    // zera, ale przejazd trwa dalej. Ta sama zasada, co przy czasie trwania
    // i sladzie trasy: restart nie kasuje jazdy.
    RideNoise beforeRestart = makeRideNoise(103.0f, 71.0f, 5, 0, 1);

    // Po restarcie mikrofon nie ma jeszcze nic.
    beforeRestart.raiseTo(RideNoise{});
    TEST_ASSERT_EQUAL_INT16(1030, beforeRestart.maxDb10);
    TEST_ASSERT_EQUAL_UINT16(71, beforeRestart.atSpeedKmh);
    TEST_ASSERT_EQUAL_UINT16(5, beforeRestart.clipped);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_wazenie_a_trafia_w_norme);
    RUN_TEST(test_wazenie_a_przy_8k_odbiega_o_znane_pol_decybela);
    RUN_TEST(test_usredniamy_moc_a_nie_decybele);
    RUN_TEST(test_fast_opada_34_decybele_na_sekunde);
    RUN_TEST(test_pik_jest_odrzucany);
    RUN_TEST(test_warkot_jest_zachowany);
    RUN_TEST(test_za_krotki_nie_liczy_sie);
    RUN_TEST(test_dokladnie_piec_sekund);
    RUN_TEST(test_czyste_minimum_odrzuciloby_piec_sekund);
    RUN_TEST(test_seria_pikow_to_nie_warkot);
    RUN_TEST(test_monotonicznosc);
    RUN_TEST(test_niepelne_okno_nic_nie_orzeka);
    RUN_TEST(test_zamknieta_bramka_nie_ustanawia_rekordu);
    RUN_TEST(test_krecenie_gazem_na_swiatlach_nie_wchodzi_do_rekordu);
    RUN_TEST(test_cisza_przed_bramka_tez_nie_psuje_wyniku);
    RUN_TEST(test_predkosc_zapamietana_w_chwili_rekordu);
    RUN_TEST(test_przesterowanie_jest_liczone);
    RUN_TEST(test_zgubione_bloki_sa_liczone);
    RUN_TEST(test_reset_zaczyna_przejazd_od_zera);
    RUN_TEST(test_okno_krotsze_niz_domyslne_ma_wlasna_pojemnosc);
    RUN_TEST(test_pakowanie_jest_odwracalne);
    RUN_TEST(test_brak_pomiaru_przezywa_zapis);
    RUN_TEST(test_poziom_ujemny_przezywa_zapis);
    RUN_TEST(test_liczniki_sie_nasycaja);
    RUN_TEST(test_rekord_bierze_predkosc_razem_z_poziomem);
    RUN_TEST(test_restart_w_trakcie_jazdy_nie_kasuje_rekordu);
    return UNITY_END();
}
