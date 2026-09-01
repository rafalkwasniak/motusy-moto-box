// Motusy Moto Box — testy harmonogramu wysylki i tozsamosci punktu dostepowego.
//
// Obie rzeczy sa decyzjami energetycznymi albo uzytkowymi, nie sieciowymi:
// kiedy wolno wlaczyc radio i co uzytkownik przepisze z ekranu do telefonu.
// Dlatego daja sie sprawdzic na komputerze, bez ani jednego pakietu.

#include <unity.h>

#include <cstring>

#include "PortalIdentity.h"
#include "UploadScheduler.h"

using namespace telemetry;

namespace {

/// Komplet warunkow "mozna wysylac": konfiguracja, zaleglosci, prad.
bool ready(const UploadScheduler& scheduler, uint32_t nowMs) {
    return scheduler.shouldAttempt(true, true, true, nowMs);
}

}  // namespace

void setUp() {}
void tearDown() {}

// ── Harmonogram wysylki ────────────────────────────────────────────────────

void test_scheduler_needs_config_pending_and_power() {
    UploadScheduler scheduler;

    TEST_ASSERT_FALSE(scheduler.shouldAttempt(false, true, true, 1000));   // bez tokena
    TEST_ASSERT_FALSE(scheduler.shouldAttempt(true, false, true, 1000));   // nic nie czeka
    TEST_ASSERT_FALSE(scheduler.shouldAttempt(true, true, false, 1000));   // czuwanie
    TEST_ASSERT_TRUE(scheduler.shouldAttempt(true, true, true, 1000));
}

/// Motocykl pod sklepem nie ma sieci domowej — kazda kolejna proba jest
/// mniej prawdopodobna, wiec odstep rosnie dwukrotnie.
void test_scheduler_backs_off_after_failures() {
    UploadScheduler scheduler;

    scheduler.onOutcome(UploadOutcome::TemporaryFailure, 1000);
    TEST_ASSERT_FALSE(ready(scheduler, 1000 + 29000));
    TEST_ASSERT_TRUE(ready(scheduler, 1000 + 30000));

    scheduler.onOutcome(UploadOutcome::TemporaryFailure, 100000);
    TEST_ASSERT_FALSE(ready(scheduler, 100000 + 59000));
    TEST_ASSERT_TRUE(ready(scheduler, 100000 + 60000));

    scheduler.onOutcome(UploadOutcome::TemporaryFailure, 200000);
    TEST_ASSERT_FALSE(ready(scheduler, 200000 + 119000));
    TEST_ASSERT_TRUE(ready(scheduler, 200000 + 120000));

    TEST_ASSERT_EQUAL_UINT32(3, scheduler.failures());
}

void test_scheduler_backoff_stops_growing_at_limit() {
    UploadScheduler scheduler;

    for (int i = 0; i < 20; ++i) {
        scheduler.onOutcome(UploadOutcome::TemporaryFailure, 0);
    }

    // Kwadrans i ani sekundy wiecej — dalsze podwajanie nic nie wnosi,
    // a odstep liczony w godzinach wygladalby jak zawieszona wysylka.
    TEST_ASSERT_EQUAL_UINT32(15UL * 60UL * 1000UL, scheduler.msUntilNextAttempt(0));
}

void test_scheduler_success_clears_backoff() {
    UploadScheduler scheduler;
    scheduler.onOutcome(UploadOutcome::TemporaryFailure, 1000);
    scheduler.onOutcome(UploadOutcome::TemporaryFailure, 1000);

    scheduler.onOutcome(UploadOutcome::Success, 5000);

    TEST_ASSERT_EQUAL_UINT32(0, scheduler.failures());
    TEST_ASSERT_TRUE(ready(scheduler, 5001));
}

/// Zly token to jedyny blad, ktorego ponawianie nie naprawi — a budzenie radia
/// w kolko rozladowaloby bateria w jedna noc.
void test_scheduler_stops_completely_after_auth_rejection() {
    UploadScheduler scheduler;

    scheduler.onOutcome(UploadOutcome::AuthRejected, 1000);

    TEST_ASSERT_TRUE(scheduler.isBlocked());
    TEST_ASSERT_FALSE(ready(scheduler, 1000));
    TEST_ASSERT_FALSE(ready(scheduler, 1000 + 24UL * 3600UL * 1000UL));  // doba pozniej

    // Poprawiony token zdejmuje blokade natychmiast — uzytkownik wlasnie zrobil
    // jedyna rzecz, ktora mogla pomoc.
    scheduler.onConfigChanged();
    TEST_ASSERT_FALSE(scheduler.isBlocked());
    TEST_ASSERT_TRUE(ready(scheduler, 2000));
}

/// millis() przekreca sie po 49 dniach. Urzadzenie na motocyklu stoi
/// podlaczone tygodniami, wiec ten moment naprawde nadejdzie.
void test_scheduler_survives_millis_rollover() {
    UploadScheduler scheduler;
    const uint32_t nearMax = 0xFFFFFF00UL;

    scheduler.onOutcome(UploadOutcome::TemporaryFailure, nearMax);

    // 30 s pozniej licznik jest juz po drugiej stronie zera.
    const uint32_t after = nearMax + 30000UL;
    TEST_ASSERT_TRUE(ready(scheduler, after));
    TEST_ASSERT_FALSE(ready(scheduler, nearMax + 1000UL));
}

// ── Tozsamosc punktu dostepowego ───────────────────────────────────────────

void test_portal_ssid_ends_with_device_id() {
    char ssid[16];

    portalSsid("a1b2c3d4e5f6", ssid, sizeof(ssid));
    TEST_ASSERT_EQUAL_STRING("MOTOBOX-E5F6", ssid);

    // Urzadzenie bez odczytanego identyfikatora nadal daje sie skonfigurowac.
    portalSsid("", ssid, sizeof(ssid));
    TEST_ASSERT_EQUAL_STRING("MOTOBOX-0000", ssid);
}

/// To samo urzadzenie ma po miesiacach dac to samo haslo — inaczej telefon
/// nie polaczy sie z zapamietanej sieci, a kartki z haslem juz nie ma.
void test_portal_password_is_repeatable() {
    char first[16];
    char second[16];

    portalPassword("a1b2c3d4e5f6", first, sizeof(first));
    portalPassword("a1b2c3d4e5f6", second, sizeof(second));

    TEST_ASSERT_EQUAL_STRING(first, second);
    TEST_ASSERT_EQUAL_UINT32(kPortalPasswordLen, std::strlen(first));
}

void test_portal_password_differs_between_devices() {
    char a[16];
    char b[16];

    portalPassword("a1b2c3d4e5f6", a, sizeof(a));
    portalPassword("a1b2c3d4e5f7", b, sizeof(b));

    TEST_ASSERT_TRUE(std::strcmp(a, b) != 0);
}

/// Haslo przepisuje sie wzrokiem z ekranu 240x135, wiec nie moze zawierac
/// znakow mylacych sie parami.
void test_portal_password_avoids_confusable_characters() {
    const char* devices[] = {"a1b2c3d4e5f6", "000000000000", "ffffffffffff", ""};

    for (const char* device : devices) {
        char password[16];
        portalPassword(device, password, sizeof(password));

        TEST_ASSERT_EQUAL_UINT32(kPortalPasswordLen, std::strlen(password));
        for (const char* p = password; *p != '\0'; ++p) {
            TEST_ASSERT_TRUE(*p != 'O' && *p != '0' && *p != 'I' && *p != '1');
            const bool isUpperLetter = *p >= 'A' && *p <= 'Z';
            const bool isDigit = *p >= '2' && *p <= '9';
            TEST_ASSERT_TRUE(isUpperLetter || isDigit);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_scheduler_needs_config_pending_and_power);
    RUN_TEST(test_scheduler_backs_off_after_failures);
    RUN_TEST(test_scheduler_backoff_stops_growing_at_limit);
    RUN_TEST(test_scheduler_success_clears_backoff);
    RUN_TEST(test_scheduler_stops_completely_after_auth_rejection);
    RUN_TEST(test_scheduler_survives_millis_rollover);

    RUN_TEST(test_portal_ssid_ends_with_device_id);
    RUN_TEST(test_portal_password_is_repeatable);
    RUN_TEST(test_portal_password_differs_between_devices);
    RUN_TEST(test_portal_password_avoids_confusable_characters);
    return UNITY_END();
}
