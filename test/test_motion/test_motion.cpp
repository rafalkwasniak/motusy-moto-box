// Motusy Moto Box — testy jednostkowe algorytmow (srodowisko native).
//
// Uruchomienie:  pio test -e native
//
// Najwazniejszy test to `test_steady_corner_does_not_straighten_bike` — pilnuje
// wlasnosci, ktora odroznia ten filtr od naiwnego filtru komplementarnego.

#include <unity.h>

#include <cmath>
#include <vector>

#include "MountCalibration.h"
#include "Orientation.h"
#include "RideMetrics.h"

using namespace motion;

namespace {

constexpr float kDt = 0.01f;  // 100 Hz, zgodnie z ODR przyjetym dla BMI270

/// Kalibracja dla urzadzenia zamontowanego "idealnie": os Z urzadzenia w gore,
/// os X urzadzenia do przodu motocykla.
MountCalibration idealMount() {
    MountCalibration mount;
    const bool ok = mount.calibrateFromRest({0.0f, 0.0f, 1.0f}, ForwardAxis::DeviceXPlus);
    TEST_ASSERT_TRUE(ok);
    return mount;
}

/// Model motocykla w rownowadze przy zadanym przechyle: sila wlasciwa lezy wzdluz
/// osi pionowej motocykla i ma modul 1/cos(phi). Wlasnie ta wlasnosc sprawia,
/// ze akcelerometr nie widzi przechylu w zakrecie.
Vec3 equilibriumAccelBike(float rollRad) {
    return {0.0f, 0.0f, -1.0f / std::cos(rollRad)};
}

/// Predkosci katowe w ukladzie motocykla dla ustalonego zakretu o zadanym
/// przechyle i tempie przechylania.
Vec3 turnRatesBike(float rollRad, float verticalTurnRateRadS, float rollRateRadS) {
    return {rollRateRadS,
            verticalTurnRateRadS * std::sin(rollRad),
            verticalTurnRateRadS * std::cos(rollRad)};
}

/// Zamienia wektor z ukladu motocykla na uklad urzadzenia przy montazu idealnym.
/// Macierz idealMount() to [fwd; right; down] = [(1,0,0); (0,-1,0); (0,0,-1)],
/// ktora jest sama sobie odwrotnoscia.
Vec3 bikeToDeviceIdeal(const Vec3& v) {
    return {v.x, -v.y, -v.z};
}

struct Simulator {
    Orientation filter;
    unsigned long nowMs = 0;

    Simulator() { filter.setMount(idealMount()); }

    void step(float rollRad, float verticalTurnRateRadS, float rollRateRadS) {
        ImuSample sample;
        sample.accelG = bikeToDeviceIdeal(equilibriumAccelBike(rollRad));
        sample.gyroRadS = bikeToDeviceIdeal(turnRatesBike(rollRad, verticalTurnRateRadS, rollRateRadS));
        sample.timestampMs = nowMs;
        filter.update(sample, kDt);
        nowMs += static_cast<unsigned long>(kDt * 1000.0f);
    }

    void run(float seconds, float rollRad, float verticalTurnRateRadS, float rollRateRadS) {
        const int steps = static_cast<int>(seconds / kDt);
        for (int i = 0; i < steps; ++i) step(rollRad, verticalTurnRateRadS, rollRateRadS);
    }
};

}  // namespace

void setUp() {}
void tearDown() {}

// ─────────────────────────────────────────────────────────────────────────────
// Kalibracja montazu
// ─────────────────────────────────────────────────────────────────────────────

void test_mount_calibration_maps_rest_to_bike_down_axis() {
    const MountCalibration mount = idealMount();
    const Vec3 bike = mount.toBikeFrame({0.0f, 0.0f, 1.0f});

    // W spoczynku pionowo akcelerometr w ukladzie motocykla to (0, 0, -1 g).
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, bike.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, bike.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, bike.z);
}

void test_mount_calibration_handles_rotated_device() {
    // Urzadzenie obrocone o 90 stopni: os Y urzadzenia w gore, os X nadal do przodu.
    MountCalibration mount;
    TEST_ASSERT_TRUE(mount.calibrateFromRest({0.0f, 1.0f, 0.0f}, ForwardAxis::DeviceXPlus));

    const Vec3 bike = mount.toBikeFrame({0.0f, 1.0f, 0.0f});
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, bike.x);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, bike.y);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, bike.z);
}

void test_mount_calibration_rejects_motion_during_calibration() {
    MountCalibration mount;
    // Modul daleki od 1 g — motocykl sie ruszal.
    TEST_ASSERT_FALSE(mount.calibrateFromRest({0.0f, 0.0f, 1.6f}, ForwardAxis::DeviceXPlus));
    TEST_ASSERT_FALSE(mount.isCalibrated());
}

void test_mount_calibration_rejects_vertical_forward_axis() {
    MountCalibration mount;
    // Deklarowany "przod" pokrywa sie z pionem — trzeci stopien swobody
    // nie da sie wyznaczyc.
    TEST_ASSERT_FALSE(mount.calibrateFromRest({0.0f, 0.0f, 1.0f}, ForwardAxis::DeviceZPlus));
}

// ─────────────────────────────────────────────────────────────────────────────
// Estymacja orientacji
// ─────────────────────────────────────────────────────────────────────────────

void test_stationary_upright_reads_zero_lean() {
    Simulator sim;
    sim.run(2.0f, 0.0f, 0.0f, 0.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, sim.filter.state().rollDeg());
    TEST_ASSERT_TRUE(sim.filter.state().stationary);
}

void test_roll_in_is_tracked_by_gyro() {
    Simulator sim;
    sim.run(1.0f, 0.0f, 0.0f, 0.0f);  // postoj

    // Przechylanie w prawo tempem 40 stopni/s przez 1 s.
    const float rollRate = degToRad(40.0f);
    const int steps = static_cast<int>(1.0f / kDt);
    for (int i = 0; i < steps; ++i) {
        const float roll = degToRad(40.0f) * (static_cast<float>(i) / static_cast<float>(steps));
        sim.step(roll, 0.0f, rollRate);
    }

    TEST_ASSERT_FLOAT_WITHIN(3.0f, 40.0f, sim.filter.state().rollDeg());
}

/// NAJWAZNIEJSZY TEST PROJEKTU.
///
/// Motocykl wchodzi w zakret i utrzymuje przechyl 40 stopni przez 5 sekund.
/// Akcelerometr przez caly ten czas pokazuje "pionowo" — naiwny filtr
/// komplementarny wyprostowalby motocykl do 0 stopni. Bramka musi to zablokowac.
void test_steady_corner_does_not_straighten_bike() {
    Simulator sim;
    sim.run(1.0f, 0.0f, 0.0f, 0.0f);

    const float targetRoll = degToRad(40.0f);
    const float rollRate = degToRad(40.0f);
    const int rollInSteps = static_cast<int>(1.0f / kDt);
    for (int i = 0; i < rollInSteps; ++i) {
        const float roll = targetRoll * (static_cast<float>(i) / static_cast<float>(rollInSteps));
        sim.step(roll, 0.0f, rollRate);
    }

    const float rollAfterEntry = sim.filter.state().rollDeg();

    // Ustalony zakret: przechyl staly, motocykl skreca z predkoscia katowa
    // wynikajaca z rownowagi przy 40 stopniach i predkosci 20 m/s.
    const float speed = 20.0f;
    const float turnRate = kGravity * std::tan(targetRoll) / speed;
    sim.run(5.0f, targetRoll, turnRate, 0.0f);

    const float rollAfterCorner = sim.filter.state().rollDeg();

    TEST_ASSERT_FALSE_MESSAGE(sim.filter.state().accelCorrectionActive,
                              "Bramka akcelerometru musi byc zamknieta w zakrecie");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2.0f, rollAfterEntry, rollAfterCorner,
                                     "Filtr wyprostowal motocykl w ustalonym zakrecie");
    TEST_ASSERT_TRUE_MESSAGE(rollAfterCorner > 35.0f, "Utracono przechyl w zakrecie");
}

/// Ten sam zakret, ale estymata wystartowala z bledem 15 stopni (dryft zyroskopu).
/// Bez predkosci nie da sie tego naprawic w zakrecie. Z predkoscia z GPS —
/// korekcja z ustalonego zakretu sciaga estymate do prawdy.
void test_speed_hint_corrects_drift_inside_corner() {
    Simulator sim;
    sim.run(1.0f, 0.0f, 0.0f, 0.0f);

    const float targetRoll = degToRad(40.0f);
    const float speed = 20.0f;
    const float turnRate = kGravity * std::tan(targetRoll) / speed;

    // Wjazd w zakret z zaniżonym tempem przechylania — symuluje dryft:
    // filtr "widzi" tylko 25 stopni, motocykl jest realnie na 40.
    const int rollInSteps = static_cast<int>(1.0f / kDt);
    for (int i = 0; i < rollInSteps; ++i) {
        const float roll = targetRoll * (static_cast<float>(i) / static_cast<float>(rollInSteps));
        ImuSample sample;
        sample.accelG = bikeToDeviceIdeal(equilibriumAccelBike(roll));
        sample.gyroRadS = bikeToDeviceIdeal(turnRatesBike(roll, 0.0f, degToRad(25.0f)));
        sample.timestampMs = sim.nowMs;
        sim.filter.update(sample, kDt);
        sim.nowMs += 10;
    }

    const float drifted = sim.filter.state().rollDeg();
    TEST_ASSERT_TRUE_MESSAGE(drifted < 32.0f, "Scenariusz testowy nie wytworzyl dryftu");

    // Wchodzi GPS: predkosc odswiezana co 100 ms.
    for (int i = 0; i < 500; ++i) {
        if (i % 10 == 0) sim.filter.setSpeedHint(speed, sim.nowMs);
        sim.step(targetRoll, turnRate, 0.0f);
    }

    TEST_ASSERT_TRUE_MESSAGE(sim.filter.state().turnCorrectionActive,
                             "Korekcja z ustalonego zakretu nie zadzialala");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(3.0f, 40.0f, sim.filter.state().rollDeg(),
                                     "GPS nie skorygowal dryftu w zakrecie");
}

void test_lean_from_coordinated_turn_matches_physics() {
    // v = 20 m/s, omega = 0.3 rad/s  ->  atan(20*0.3/9.80665) = 31.45 stopnia
    const float lean = Orientation::leanFromCoordinatedTurn(20.0f, 0.3f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 31.45f, radToDeg(lean));
}

void test_gyro_bias_is_learned_at_rest() {
    Simulator sim;
    const Vec3 bias{degToRad(0.8f), degToRad(-0.5f), degToRad(0.3f)};

    for (int i = 0; i < 2000; ++i) {  // 20 s postoju
        ImuSample sample;
        sample.accelG = bikeToDeviceIdeal(equilibriumAccelBike(0.0f));
        sample.gyroRadS = bias;
        sample.timestampMs = sim.nowMs;
        sim.filter.update(sample, kDt);
        sim.nowMs += 10;
    }

    const Vec3 learned = sim.filter.state().gyroBiasRadS;
    TEST_ASSERT_FLOAT_WITHIN(degToRad(0.1f), bias.x, learned.x);
    TEST_ASSERT_FLOAT_WITHIN(degToRad(0.1f), bias.y, learned.y);
    TEST_ASSERT_FLOAT_WITHIN(degToRad(0.1f), bias.z, learned.z);
}

/// Postoj na podjezdzie 10% nie moze zostac zapisany jako hamowanie -0.10 g.
void test_pitch_compensation_cancels_slope_gravity() {
    MountCalibration mount = idealMount();
    Orientation filter;
    filter.setMount(mount);

    const float pitch = std::atan(0.10f);  // podjazd 10%
    // Sila wlasciwa w ukladzie motocykla przy uniesionym przodzie, w spoczynku.
    const Vec3 accelBike{std::sin(pitch), 0.0f, -std::cos(pitch)};

    for (int i = 0; i < 1000; ++i) {
        ImuSample sample;
        sample.accelG = bikeToDeviceIdeal(accelBike);
        sample.timestampMs = static_cast<unsigned long>(i * 10);
        filter.update(sample, kDt);
    }

    TEST_ASSERT_FLOAT_WITHIN(1.0f, radToDeg(pitch), filter.state().pitchDeg());
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, filter.state().longitudinalG,
                                     "Podjazd zostal policzony jako hamowanie");
}

// ─────────────────────────────────────────────────────────────────────────────
// Wyniki jazdy
// ─────────────────────────────────────────────────────────────────────────────

void test_new_ride_clears_session_but_keeps_overall() {
    RideMetrics metrics;

    OrientationState state;
    state.rollRad = degToRad(35.0f);
    state.longitudinalG = 0.6f;
    metrics.update(state);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 35.0f, metrics.currentRide().maxLeanRightDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 35.0f, metrics.overall().maxLeanRightDeg);

    metrics.startNewRide();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.currentRide().maxLeanRightDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 35.0f, metrics.overall().maxLeanRightDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.6f, metrics.overall().maxAccelG);
}

void test_reset_clears_both_sets() {
    RideMetrics metrics;
    OrientationState state;
    state.rollRad = degToRad(-28.0f);
    state.longitudinalG = -0.75f;
    metrics.update(state);

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 28.0f, metrics.overall().maxLeanLeftDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.75f, metrics.overall().maxBrakeG);

    metrics.resetAll();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxLeanLeftDeg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxBrakeG);
}

void test_speed_record_needs_valid_fix() {
    RideMetrics metrics;

    // Probka bez fixa nie moze ustanowic rekordu, nawet jesli niesie liczbe.
    metrics.updateSpeed({180.0f, false});
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxSpeedKmh);

    metrics.updateSpeed({180.0f, true});
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 180.0f, metrics.overall().maxSpeedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 180.0f, metrics.currentRide().maxSpeedKmh);
}

void test_speed_record_rejects_noise_and_glitches() {
    RideMetrics metrics;

    // Szum GPS na postoju.
    metrics.updateSpeed({2.0f, true});
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxSpeedKmh);

    // Bledny fix po wyjezdzie z tunelu.
    metrics.updateSpeed({1200.0f, true});
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxSpeedKmh);

    metrics.updateSpeed({95.0f, true});
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 95.0f, metrics.overall().maxSpeedKmh);
}

void test_speed_follows_same_two_set_rules() {
    RideMetrics metrics;
    metrics.updateSpeed({210.0f, true});
    metrics.startNewRide();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.currentRide().maxSpeedKmh);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 210.0f, metrics.overall().maxSpeedKmh);

    metrics.updateSpeed({140.0f, true});
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 140.0f, metrics.currentRide().maxSpeedKmh);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 210.0f, metrics.overall().maxSpeedKmh,
                                     "Wolniejsza jazda nie moze obnizyc rekordu ogolnego");

    metrics.resetAll();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxSpeedKmh);
}

void test_implausible_values_are_rejected() {
    RideMetrics metrics;

    OrientationState noise;
    noise.rollRad = degToRad(1.0f);  // ponizej progu szumu
    metrics.update(noise);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxLeanRightDeg);

    OrientationState impact;
    impact.longitudinalG = 5.0f;  // uderzenie, nie przyspieszenie
    metrics.update(impact);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, metrics.overall().maxAccelG);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_mount_calibration_maps_rest_to_bike_down_axis);
    RUN_TEST(test_mount_calibration_handles_rotated_device);
    RUN_TEST(test_mount_calibration_rejects_motion_during_calibration);
    RUN_TEST(test_mount_calibration_rejects_vertical_forward_axis);

    RUN_TEST(test_stationary_upright_reads_zero_lean);
    RUN_TEST(test_roll_in_is_tracked_by_gyro);
    RUN_TEST(test_steady_corner_does_not_straighten_bike);
    RUN_TEST(test_speed_hint_corrects_drift_inside_corner);
    RUN_TEST(test_lean_from_coordinated_turn_matches_physics);
    RUN_TEST(test_gyro_bias_is_learned_at_rest);
    RUN_TEST(test_pitch_compensation_cancels_slope_gravity);

    RUN_TEST(test_new_ride_clears_session_but_keeps_overall);
    RUN_TEST(test_reset_clears_both_sets);
    RUN_TEST(test_implausible_values_are_rejected);
    RUN_TEST(test_speed_record_needs_valid_fix);
    RUN_TEST(test_speed_record_rejects_noise_and_glitches);
    RUN_TEST(test_speed_follows_same_two_set_rules);

    return UNITY_END();
}
