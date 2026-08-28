// Motusy Moto Box — testy silnika alarmu.
//
// Pilnuja czterech wlasnosci: pojedyncze drgniecie nie eskaluje (§19),
// eskalacja idzie 1 -> 2 -> 3, sygnal ciagly ma twardy limit czasu,
// a po ciszy poziom opada.

#include <unity.h>

#include "AlarmEngine.h"

using namespace guard;
using motion::ImuSample;
using motion::Vec3;

namespace {

constexpr uint32_t kStepMs = 20;

AlarmConfig testConfig() {
    AlarmConfig config;
    config.tiltThresholdDeg = 4.0f;
    config.accelThresholdG = 0.12f;
    config.sustainMs = 250;
    config.retriggerGapMs = 2000;
    config.decayMs = 60000;
    return config;
}

ImuSample still() {
    ImuSample sample;
    sample.accelG = {0.0f, 0.0f, 1.0f};
    return sample;
}

/// Motocykl odchylony o `deg` stopni od pozycji odniesienia.
ImuSample tilted(float deg) {
    ImuSample sample;
    const float rad = motion::degToRad(deg);
    sample.accelG = {std::sin(rad), 0.0f, std::cos(rad)};
    return sample;
}

/// Szarpniecie: modul przyspieszenia daleki od 1 g.
ImuSample shaken() {
    ImuSample sample;
    sample.accelG = {0.3f, 0.1f, 1.2f};
    return sample;
}

/// Przewija czas z zadana probka. Zwraca liczbe naruszen po drodze.
int run(AlarmEngine& engine, const ImuSample& sample, uint32_t& now, uint32_t durationMs) {
    int violations = 0;
    const uint32_t end = now + durationMs;
    while (now < end) {
        if (engine.update(&sample, now).violation) ++violations;
        now += kStepMs;
    }
    return violations;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_still_bike_never_triggers() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    // Dziesiec minut absolutnego spokoju.
    TEST_ASSERT_EQUAL(0, run(engine, still(), now, 600000));
    TEST_ASSERT_EQUAL(0, engine.violationCount());
}

/// §19 — pojedyncze drgniecie (przejezdzajaca ciezarowka) nie moze eskalowac.
void test_brief_jolt_is_ignored() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    // Szarpniecie na 100 ms — krocej niz sustainMs (250 ms).
    int violations = run(engine, shaken(), now, 100);
    violations += run(engine, still(), now, 5000);

    TEST_ASSERT_EQUAL_MESSAGE(0, violations, "Krotkie drgniecie wywolalo alarm");
}

void test_sustained_tilt_triggers_stage1() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    const int violations = run(engine, tilted(10.0f), now, 1000);

    TEST_ASSERT_EQUAL(1, violations);
    TEST_ASSERT_EQUAL(1, engine.violationCount());

    // W trakcie wzorca stopnia 1 glosnik faktycznie gra.
    const AlarmOutput out = engine.update(nullptr, now);
    TEST_ASSERT_TRUE(out.signalling);
}

void test_escalation_reaches_continuous_siren() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    // Trzy naruszenia przedzielone spokojem (dluzszym niz retriggerGap).
    for (int i = 0; i < 3; ++i) {
        run(engine, tilted(10.0f), now, 600);
        run(engine, still(), now, 3000);
    }

    TEST_ASSERT_EQUAL(3, engine.violationCount());
}

/// Decyzja z 2026-08-28: syrena wyje az do rozbrojenia — takze wtedy, gdy
/// zlodziej odszedl z motocyklem i ruch ustal.
void test_siren_wails_until_disarmed() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    for (int i = 0; i < 3; ++i) {
        run(engine, tilted(10.0f), now, 600);
        run(engine, still(), now, 3000);
    }

    // Pelne pięc minut ciszy — syrena ma grac dalej, z modulowana
    // czestotliwoscia w zakresie przestroju.
    const ImuSample quiet = still();
    uint16_t minFreq = 65535;
    uint16_t maxFreq = 0;
    for (uint32_t t = 0; t < 300000; t += kStepMs) {
        const AlarmOutput out = engine.update(&quiet, now);
        TEST_ASSERT_TRUE_MESSAGE(out.sirenOn, "Syrena umilkla bez rozbrojenia");
        if (out.freqHz < minFreq) minFreq = out.freqHz;
        if (out.freqHz > maxFreq) maxFreq = out.freqHz;
        now += kStepMs;
    }
    TEST_ASSERT_TRUE_MESSAGE(maxFreq - minFreq > 1000,
                             "Syrena nie jest modulowana");

    engine.disarm();
    TEST_ASSERT_FALSE(engine.update(&quiet, now).sirenOn);
}

void test_quiet_period_decays_escalation() {
    AlarmConfig config = testConfig();
    config.decayMs = 2000;  // szybszy zanik na potrzeby testu
    AlarmEngine engine{config};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    run(engine, tilted(10.0f), now, 600);
    run(engine, still(), now, 1000);
    TEST_ASSERT_EQUAL(1, engine.violationCount());

    // Po dwoch okresach ciszy licznik opada do zera.
    run(engine, still(), now, 5000);
    TEST_ASSERT_EQUAL(0, engine.violationCount());
}

void test_disarm_silences_immediately() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);
    run(engine, tilted(10.0f), now, 600);

    engine.disarm();
    const ImuSample moving = tilted(10.0f);
    const AlarmOutput out = engine.update(&moving, now);

    TEST_ASSERT_FALSE(out.signalling);
    TEST_ASSERT_FALSE(out.sirenOn);
    TEST_ASSERT_FALSE(out.violation);
}

void test_stage_limit_for_low_battery() {
    AlarmConfig config = testConfig();
    config.maxStage = 1;  // ochrona przy slabej baterii
    AlarmEngine engine{config};
    uint32_t now = 1000;
    engine.arm(Vec3{0.0f, 0.0f, 1.0f}, now);

    for (int i = 0; i < 4; ++i) {
        run(engine, tilted(10.0f), now, 600);
        run(engine, still(), now, 3000);
    }

    TEST_ASSERT_EQUAL_MESSAGE(1, engine.violationCount(),
                              "Limit stopnia przy slabej baterii nie dziala");
}

/// Uzbrojenie na bocznej stopce: odniesieniem jest pozycja z momentu uzbrojenia,
/// wiec motocykl pochylony na stopce w spoczynku NIE jest naruszeniem.
void test_reference_is_arming_position_not_vertical() {
    AlarmEngine engine{testConfig()};
    uint32_t now = 1000;

    const ImuSample onStand = tilted(15.0f);
    engine.arm(onStand.accelG, now);

    TEST_ASSERT_EQUAL_MESSAGE(0, run(engine, onStand, now, 60000),
                              "Motocykl na stopce wywolal wlasny alarm");

    // Podniesienie do pionu to odchylenie od odniesienia — alarm.
    TEST_ASSERT_EQUAL(1, run(engine, still(), now, 1000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_still_bike_never_triggers);
    RUN_TEST(test_brief_jolt_is_ignored);
    RUN_TEST(test_sustained_tilt_triggers_stage1);
    RUN_TEST(test_escalation_reaches_continuous_siren);
    RUN_TEST(test_siren_wails_until_disarmed);
    RUN_TEST(test_quiet_period_decays_escalation);
    RUN_TEST(test_disarm_silences_immediately);
    RUN_TEST(test_stage_limit_for_low_battery);
    RUN_TEST(test_reference_is_arming_position_not_vertical);
    return UNITY_END();
}
