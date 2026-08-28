// Motusy Moto Box — testy maszyny stanow urzadzenia.
//
// Najwazniejsze sa dwa: rozruch silnika nie moze zakonczyc sesji jazdy,
// a alarm nie moze sie uzbroic, dopoki jest zasilanie.

#include <unity.h>

#include "DeviceStateMachine.h"

using namespace state;

namespace {

constexpr uint32_t kArmingMs = 120000;

DeviceStateConfig testConfig() {
    DeviceStateConfig config;
    config.armingDelayMs = kArmingMs;
    config.powerLossConfirmMs = 5000;
    config.powerReturnConfirmMs = 1500;
    return config;
}

/// Przewija czas, podajac staly stan zasilania. Zwraca ostatnie zdarzenie
/// inne niz None — dzieki temu test widzi przejscie, nawet jesli po nim
/// maszyna zwrocila juz None.
DeviceEvent advance(DeviceStateMachine& fsm, bool power, bool alarmEnabled, uint32_t& now,
                    uint32_t durationMs, uint32_t stepMs = 100) {
    DeviceEvent last = DeviceEvent::None;
    const uint32_t end = now + durationMs;
    while (now < end) {
        const DeviceEvent e = fsm.update(power, alarmEnabled, false, now);
        if (e != DeviceEvent::None) last = e;
        now += stepMs;
    }
    return last;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_starts_riding_when_powered() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);
    TEST_ASSERT_EQUAL(DeviceState::Riding, fsm.state());
    TEST_ASSERT_TRUE(fsm.screenShouldBeOn());
}

void test_power_loss_enters_cooldown_then_arms() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);

    TEST_ASSERT_EQUAL(DeviceEvent::PowerLost, advance(fsm, false, true, now, 6000));
    TEST_ASSERT_EQUAL(DeviceState::Cooldown, fsm.state());
    TEST_ASSERT_TRUE(fsm.screenShouldBeOn());

    // Tuz przed uplywem dwoch minut ekran wciaz swieci.
    advance(fsm, false, true, now, kArmingMs - 20000);
    TEST_ASSERT_EQUAL(DeviceState::Cooldown, fsm.state());

    TEST_ASSERT_EQUAL(DeviceEvent::ScreenOff, advance(fsm, false, true, now, 30000));
    TEST_ASSERT_EQUAL(DeviceState::Armed, fsm.state());
    TEST_ASSERT_FALSE(fsm.screenShouldBeOn());
    TEST_ASSERT_TRUE(fsm.maySleep());
}

void test_disabled_alarm_sleeps_without_arming() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);

    advance(fsm, false, false, now, 6000);
    advance(fsm, false, false, now, kArmingMs + 5000);

    TEST_ASSERT_EQUAL(DeviceState::Idle, fsm.state());
    TEST_ASSERT_FALSE(fsm.screenShouldBeOn());
}

/// NAJWAZNIEJSZY TEST: zapad napiecia przy rozruchu silnika trwa ulamek sekundy
/// i nie moze zakonczyc sesji jazdy. Gdyby konczyl, po dwoch minutach alarm
/// zawylby w trakcie jazdy.
void test_engine_crank_dip_does_not_end_ride() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);

    // Zapad na 800 ms — krocej niz powerLossConfirmMs.
    for (uint32_t t = 0; t < 800; t += 50) {
        TEST_ASSERT_EQUAL(DeviceEvent::None, fsm.update(false, true, false, now));
        now += 50;
    }
    advance(fsm, true, true, now, 3000);

    TEST_ASSERT_EQUAL_MESSAGE(DeviceState::Riding, fsm.state(),
                              "Zapad napiecia przerwal sesje jazdy");
}

void test_power_return_starts_new_ride_from_any_state() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);

    advance(fsm, false, true, now, 6000);
    advance(fsm, false, true, now, kArmingMs + 5000);
    TEST_ASSERT_EQUAL(DeviceState::Armed, fsm.state());

    TEST_ASSERT_EQUAL(DeviceEvent::RideStarted, advance(fsm, true, true, now, 3000));
    TEST_ASSERT_EQUAL(DeviceState::Riding, fsm.state());
}

void test_alarm_never_arms_while_powered() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);

    // Pol godziny jazdy — alarm nie ma prawa sie uzbroic.
    advance(fsm, true, true, now, 1800000, 1000);

    TEST_ASSERT_EQUAL_MESSAGE(DeviceState::Riding, fsm.state(),
                              "Alarm uzbroil sie mimo obecnego zasilania");
}

void test_motion_triggers_only_when_armed() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);

    // Ruch w trakcie jazdy nie moze niczego wyzwolic.
    TEST_ASSERT_EQUAL(DeviceEvent::None, fsm.update(true, true, true, now));
    now += 100;

    advance(fsm, false, true, now, 6000);
    advance(fsm, false, true, now, kArmingMs + 5000);
    TEST_ASSERT_EQUAL(DeviceState::Armed, fsm.state());

    TEST_ASSERT_EQUAL(DeviceEvent::MotionDetected, fsm.update(false, true, true, now));
    TEST_ASSERT_EQUAL(DeviceState::Triggered, fsm.state());
}

void test_silence_returns_to_idle() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);
    advance(fsm, false, true, now, 6000);
    advance(fsm, false, true, now, kArmingMs + 5000);
    fsm.update(false, true, true, now);
    TEST_ASSERT_EQUAL(DeviceState::Triggered, fsm.state());

    fsm.silence(now);
    TEST_ASSERT_EQUAL(DeviceState::Idle, fsm.state());
}

void test_countdown_reports_remaining_time() {
    DeviceStateMachine fsm{testConfig()};
    uint32_t now = 1000;
    fsm.begin(true, now);
    advance(fsm, false, true, now, 6000);

    const uint32_t remaining = fsm.msUntilScreenOff(now);
    TEST_ASSERT_TRUE(remaining > 0);
    TEST_ASSERT_TRUE(remaining <= kArmingMs);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_starts_riding_when_powered);
    RUN_TEST(test_power_loss_enters_cooldown_then_arms);
    RUN_TEST(test_disabled_alarm_sleeps_without_arming);
    RUN_TEST(test_engine_crank_dip_does_not_end_ride);
    RUN_TEST(test_power_return_starts_new_ride_from_any_state);
    RUN_TEST(test_alarm_never_arms_while_powered);
    RUN_TEST(test_motion_triggers_only_when_armed);
    RUN_TEST(test_silence_returns_to_idle);
    RUN_TEST(test_countdown_reports_remaining_time);
    return UNITY_END();
}
