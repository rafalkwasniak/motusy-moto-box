// Motusy Moto Box — testy maszyny stanow przycisku.
//
// Najwazniejszy test to `test_long_hold_never_triggers_reset_on_the_way`:
// pilnuje wymagania §23, ktore latwo zlamac naiwna implementacja opierajaca sie
// na pressedFor().

#include <unity.h>

#include "ButtonFsm.h"

using namespace input;

namespace {

/// Symuluje trzymanie przycisku przez zadany czas, a nastepnie puszczenie.
/// Zwraca akcje rozpoznana w momencie puszczenia oraz — przez `sawAction` —
/// informacje, czy jakakolwiek akcja odpalila sie WCZESNIEJ, w trakcie trzymania.
ButtonAction holdAndRelease(ButtonFsm& fsm, uint32_t durationMs, bool& sawEarlyAction) {
    sawEarlyAction = false;
    uint32_t now = 1000;

    fsm.update(true, now);
    for (uint32_t elapsed = 0; elapsed <= durationMs; elapsed += 50) {
        if (fsm.update(true, now + elapsed) != ButtonAction::None) sawEarlyAction = true;
    }
    return fsm.update(false, now + durationMs);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_short_press_toggles_alarm() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::ShortPress, holdAndRelease(fsm, 200, early));
    TEST_ASSERT_FALSE(early);
}

void test_medium_hold_resets_results() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::MediumHold, holdAndRelease(fsm, 4000, early));
}

/// §23 — droga do kalibracji nie moze prowadzic przez reset wynikow.
void test_long_hold_never_triggers_reset_on_the_way() {
    ButtonFsm fsm;
    bool early = false;
    const ButtonAction action = holdAndRelease(fsm, 12000, early);

    TEST_ASSERT_EQUAL_MESSAGE(ButtonAction::LongHold, action,
                              "Przytrzymanie 12 s musi dac kalibracje");
    TEST_ASSERT_FALSE_MESSAGE(early,
                              "Zadna akcja nie moze odpalic w trakcie trzymania");
}

/// Ta sama zasada przy czwartym progu: droga do integracji wiedzie przez reset
/// wynikow I kalibracje, a nie moze uruchomic zadnej z nich.
void test_integration_hold_passes_through_reset_and_calibration() {
    ButtonFsm fsm;
    bool early = false;
    const ButtonAction action = holdAndRelease(fsm, 18000, early);

    TEST_ASSERT_EQUAL_MESSAGE(ButtonAction::ExtraHold, action,
                              "Najdluzsze przytrzymanie musi dac integracje");
    TEST_ASSERT_FALSE_MESSAGE(early,
                              "Ani reset, ani kalibracja nie moga odpalic po drodze");
}

/// Progi urzadzenia (2/4/6 s) sa krotsze niz domyslne — czwarty prog musi
/// dzialac takze na nich, bo to one trafiaja na motocykl.
void test_device_thresholds_reach_integration() {
    ButtonFsmConfig config;
    config.mediumHoldMs = 2000;
    config.longHoldMs = 4000;
    config.extraHoldMs = 6000;
    ButtonFsm fsm{config};
    bool early = false;

    TEST_ASSERT_EQUAL(ButtonAction::MediumHold, holdAndRelease(fsm, 3000, early));
    TEST_ASSERT_EQUAL(ButtonAction::LongHold, holdAndRelease(fsm, 5000, early));
    TEST_ASSERT_EQUAL(ButtonAction::ExtraHold, holdAndRelease(fsm, 7000, early));
    TEST_ASSERT_FALSE(early);
}

void test_bounce_is_ignored() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::None, holdAndRelease(fsm, 10, early));
}

/// Uzytkownik musi widziec, co sie stanie, zanim puscil przycisk.
void test_pending_action_advances_through_thresholds() {
    ButtonFsm fsm;
    uint32_t now = 5000;

    TEST_ASSERT_EQUAL(ButtonAction::None, fsm.pendingAction());

    fsm.update(true, now);
    fsm.update(true, now + 500);
    TEST_ASSERT_EQUAL(ButtonAction::ShortPress, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::MediumHold, fsm.nextAction());
    TEST_ASSERT_EQUAL_UINT32(2500, fsm.msToNextThreshold());

    fsm.update(true, now + 3500);
    TEST_ASSERT_EQUAL(ButtonAction::MediumHold, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::LongHold, fsm.nextAction());
    TEST_ASSERT_EQUAL_UINT32(6500, fsm.msToNextThreshold());

    fsm.update(true, now + 10500);
    TEST_ASSERT_EQUAL(ButtonAction::LongHold, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::ExtraHold, fsm.nextAction());
    TEST_ASSERT_EQUAL_UINT32(4500, fsm.msToNextThreshold());

    fsm.update(true, now + 15500);
    TEST_ASSERT_EQUAL(ButtonAction::ExtraHold, fsm.pendingAction());
    TEST_ASSERT_EQUAL_MESSAGE(ButtonAction::None, fsm.nextAction(),
                              "Integracja jest ostatnim progiem");
    TEST_ASSERT_EQUAL_UINT32(0, fsm.msToNextThreshold());
}

void test_release_clears_state() {
    ButtonFsm fsm;
    uint32_t now = 100;
    fsm.update(true, now);
    fsm.update(true, now + 5000);
    fsm.update(false, now + 5000);

    TEST_ASSERT_FALSE(fsm.isPressed());
    TEST_ASSERT_EQUAL_UINT32(0, fsm.heldMs());
    TEST_ASSERT_EQUAL(ButtonAction::None, fsm.pendingAction());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_short_press_toggles_alarm);
    RUN_TEST(test_medium_hold_resets_results);
    RUN_TEST(test_long_hold_never_triggers_reset_on_the_way);
    RUN_TEST(test_integration_hold_passes_through_reset_and_calibration);
    RUN_TEST(test_device_thresholds_reach_integration);
    RUN_TEST(test_bounce_is_ignored);
    RUN_TEST(test_pending_action_advances_through_thresholds);
    RUN_TEST(test_release_clears_state);
    return UNITY_END();
}
