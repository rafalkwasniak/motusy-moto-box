// Motusy Moto Box — testy maszyny stanow przycisku.
//
// Najwazniejszy test to `test_droga_do_integracji_nie_odpala_niczego_po_drodze`:
// pilnuje wymagania §23, ktore latwo zlamac naiwna implementacja opierajaca sie
// na pressedFor(). Przy pieciu szczeblach droga do integracji prowadzi przez
// slad, reset wynikow I kalibracje — gdyby akcje odpalaly sie w trakcie
// trzymania, kazde wejscie w konfiguracje kasowaloby rekordy po drodze.

#include <unity.h>

#include "ButtonFsm.h"

using namespace input;

namespace {

/// Symuluje trzymanie przycisku przez zadany czas, a nastepnie puszczenie.
/// Zwraca akcje rozpoznana w momencie puszczenia oraz — przez `sawEarlyAction` —
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

// ── Szczeble drabinki ──────────────────────────────────────────────────────

void test_klik_przelacza_alarm() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::Alarm, holdAndRelease(fsm, 200, early));
    TEST_ASSERT_FALSE(early);
}

void test_slad_lezy_zaraz_po_alarmie() {
    // Kolejnosc wynika z czestosci uzycia: alarm i slad to dwa przelaczniki
    // uzywane regularnie, wiec sa najplytszymi szczeblami.
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::Track, holdAndRelease(fsm, 3000, early));
    TEST_ASSERT_FALSE(early);
}

void test_reset_wynikow_po_sladzie() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::Reset, holdAndRelease(fsm, 5000, early));
}

void test_kalibracja_przedostatnia() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::Calibration, holdAndRelease(fsm, 7000, early));
    TEST_ASSERT_FALSE(early);
}

/// §23 — sedno calej maszyny.
void test_droga_do_integracji_nie_odpala_niczego_po_drodze() {
    ButtonFsm fsm;
    bool early = false;
    const ButtonAction action = holdAndRelease(fsm, 9000, early);

    TEST_ASSERT_EQUAL_MESSAGE(ButtonAction::Integration, action,
                              "Najdluzsze przytrzymanie musi dac integracje");
    TEST_ASSERT_FALSE_MESSAGE(early,
                              "Ani slad, ani reset, ani kalibracja nie moga odpalic po drodze");
}

void test_drganie_styku_jest_ignorowane() {
    ButtonFsm fsm;
    bool early = false;
    TEST_ASSERT_EQUAL(ButtonAction::None, holdAndRelease(fsm, 10, early));
}

// ── Drabinka jest danymi, nie kodem ────────────────────────────────────────

void test_wlasna_drabinka_jest_respektowana() {
    // Cala racja bytu tabeli: zmiana progow i akcji nie wymaga ruszania
    // maszyny stanow.
    ButtonFsmConfig config;
    config.rungCount = 3;
    config.rungs[0] = {0, ButtonAction::Alarm};
    config.rungs[1] = {500, ButtonAction::Track};
    config.rungs[2] = {900, ButtonAction::Integration};

    ButtonFsm fsm{config};
    bool early = false;

    TEST_ASSERT_EQUAL(ButtonAction::Alarm, holdAndRelease(fsm, 100, early));
    TEST_ASSERT_EQUAL(ButtonAction::Track, holdAndRelease(fsm, 600, early));
    TEST_ASSERT_EQUAL(ButtonAction::Integration, holdAndRelease(fsm, 5000, early));
    TEST_ASSERT_FALSE(early);
}

// ── Podpowiedz na ekranie ──────────────────────────────────────────────────

/// Uzytkownik musi widziec, co sie stanie, zanim puscil przycisk.
void test_podpowiedz_idzie_przez_wszystkie_szczeble() {
    ButtonFsm fsm;
    const uint32_t now = 5000;

    TEST_ASSERT_EQUAL(ButtonAction::None, fsm.pendingAction());

    fsm.update(true, now);

    fsm.update(true, now + 500);
    TEST_ASSERT_EQUAL(ButtonAction::Alarm, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::Track, fsm.nextAction());
    TEST_ASSERT_EQUAL_UINT32(1500, fsm.msToNextThreshold());

    fsm.update(true, now + 3500);
    TEST_ASSERT_EQUAL(ButtonAction::Track, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::Reset, fsm.nextAction());
    TEST_ASSERT_EQUAL_UINT32(500, fsm.msToNextThreshold());

    fsm.update(true, now + 5500);
    TEST_ASSERT_EQUAL(ButtonAction::Reset, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::Calibration, fsm.nextAction());

    fsm.update(true, now + 7500);
    TEST_ASSERT_EQUAL(ButtonAction::Calibration, fsm.pendingAction());
    TEST_ASSERT_EQUAL(ButtonAction::Integration, fsm.nextAction());

    fsm.update(true, now + 9500);
    TEST_ASSERT_EQUAL(ButtonAction::Integration, fsm.pendingAction());
    TEST_ASSERT_EQUAL_MESSAGE(ButtonAction::None, fsm.nextAction(),
                              "Integracja jest ostatnim szczeblem");
    TEST_ASSERT_EQUAL_UINT32(0, fsm.msToNextThreshold());
}

void test_granice_szczebla_dla_paska_postepu() {
    // Ekran rysuje z tych dwoch liczb postep W OBREBIE szczebla. Bez nich
    // musialby powtorzyc cala drabinke u siebie.
    ButtonFsm fsm;
    const uint32_t now = 100;
    fsm.update(true, now);

    fsm.update(true, now + 3500);
    TEST_ASSERT_EQUAL_UINT32(2000, fsm.rungStartMs());
    TEST_ASSERT_EQUAL_UINT32(4000, fsm.nextRungStartMs());
}

void test_na_ostatnim_szczeblu_pasek_stoi_pelny() {
    ButtonFsm fsm;
    const uint32_t now = 100;
    fsm.update(true, now);
    fsm.update(true, now + 12000);

    // Rowne granice znacza "nie ma dokad isc" — ekran rysuje wtedy pelny pasek.
    TEST_ASSERT_EQUAL_UINT32(fsm.rungStartMs(), fsm.nextRungStartMs());
}

void test_puszczenie_czysci_stan() {
    ButtonFsm fsm;
    const uint32_t now = 100;
    fsm.update(true, now);
    fsm.update(true, now + 5000);
    fsm.update(false, now + 5000);

    TEST_ASSERT_FALSE(fsm.isPressed());
    TEST_ASSERT_EQUAL_UINT32(0, fsm.heldMs());
    TEST_ASSERT_EQUAL(ButtonAction::None, fsm.pendingAction());
}

// ── Etykiety ───────────────────────────────────────────────────────────────

void test_kazda_akcja_ma_etykiete() {
    // Pusty napis na ekranie wyboru akcji znaczylby, ze uzytkownik trzyma
    // przycisk i nie wie, co sie stanie po puszczeniu.
    const ButtonAction actions[] = {ButtonAction::Alarm, ButtonAction::Track,
                                    ButtonAction::Reset, ButtonAction::Calibration,
                                    ButtonAction::Integration};
    for (ButtonAction action : actions) {
        TEST_ASSERT_TRUE(actionLabel(action)[0] != '\0');
    }
    TEST_ASSERT_EQUAL_STRING("", actionLabel(ButtonAction::None));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_klik_przelacza_alarm);
    RUN_TEST(test_slad_lezy_zaraz_po_alarmie);
    RUN_TEST(test_reset_wynikow_po_sladzie);
    RUN_TEST(test_kalibracja_przedostatnia);
    RUN_TEST(test_droga_do_integracji_nie_odpala_niczego_po_drodze);
    RUN_TEST(test_drganie_styku_jest_ignorowane);
    RUN_TEST(test_wlasna_drabinka_jest_respektowana);
    RUN_TEST(test_podpowiedz_idzie_przez_wszystkie_szczeble);
    RUN_TEST(test_granice_szczebla_dla_paska_postepu);
    RUN_TEST(test_na_ostatnim_szczeblu_pasek_stoi_pelny);
    RUN_TEST(test_puszczenie_czysci_stan);
    RUN_TEST(test_kazda_akcja_ma_etykiete);
    return UNITY_END();
}
