// Motusy Moto Box — testy formatu przesylki telemetrycznej.
//
// Ten format jest kontraktem z API po stronie serwera, wiec testy sprawdzaja
// doslowna tresc, a nie tylko "czy sie da sparsowac". Zmiana ksztaltu JSON-a
// ma tu psuc testy — to jedyny sygnal, ze trzeba ruszyc takze druga strone.

#include <unity.h>

#include <cstring>

#include "ConfigCommand.h"
#include "IntegrationConfig.h"
#include "TelemetryJson.h"

using namespace telemetry;

namespace {

DeviceIdentity testDevice() {
    DeviceIdentity device;
    device.deviceId = "a1b2c3d4e5f6";
    device.firmware = "1.0.0";
    device.calibrated = true;
    return device;
}

/// Przejazd o wartosciach dokladnie reprezentowanych binarnie — test ma
/// sprawdzac format, a nie zaokraglanie liczb zmiennoprzecinkowych.
RideRecord testRide(uint32_t seq) {
    RideRecord ride;
    ride.seq = seq;
    ride.durationS = 1832;
    ride.values.maxLeanLeftDeg = 42.0f;
    ride.values.maxLeanRightDeg = 38.0f;
    ride.values.maxAccelG = 0.75f;
    ride.values.maxBrakeG = 0.5f;
    ride.values.maxSpeedKmh = 0.0f;
    return ride;
}

}  // namespace

void setUp() {}
void tearDown() {}

/// Stan na dzis: bez GPS nie ma ani daty, ani predkosci.
void test_payload_without_gps_sends_nulls() {
    const RideRecord rides[] = {testRide(7)};
    char out[kMaxPayloadBytes];

    const size_t len = buildPayload(testDevice(), rides, 1, out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING(
        "{\"device_id\":\"a1b2c3d4e5f6\",\"fw\":\"1.0.0\",\"calibrated\":true,\"rides\":["
        "{\"seq\":7,\"recorded_at\":null,\"duration_s\":1832,"
        "\"lean_left_deg\":42.0,\"lean_right_deg\":38.0,"
        "\"accel_g\":0.75,\"brake_g\":0.50,\"speed_kmh\":null}]}",
        out);
    TEST_ASSERT_EQUAL_UINT32(std::strlen(out), len);
}

/// Po dolozeniu GPS te same pola wypelniaja sie liczbami — ksztalt sie nie zmienia.
void test_payload_with_gps_fills_time_and_speed() {
    RideRecord ride = testRide(8);
    ride.recordedAt = 1756400000LL;
    ride.values.maxSpeedKmh = 137.0f;
    char out[kMaxPayloadBytes];

    const size_t len = buildPayload(testDevice(), &ride, 1, out, sizeof(out));

    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(out, "\"recorded_at\":1756400000"));
    TEST_ASSERT_NOT_NULL(std::strstr(out, "\"speed_kmh\":137.0"));
}

void test_payload_keeps_ride_order() {
    const RideRecord rides[] = {testRide(3), testRide(4)};
    char out[kMaxPayloadBytes];

    TEST_ASSERT_TRUE(buildPayload(testDevice(), rides, 2, out, sizeof(out)) > 0);

    const char* first = std::strstr(out, "\"seq\":3");
    const char* second = std::strstr(out, "\"seq\":4");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_TRUE(first < second);
}

void test_empty_payload_is_valid_json() {
    char out[kMaxPayloadBytes];

    TEST_ASSERT_TRUE(buildPayload(testDevice(), nullptr, 0, out, sizeof(out)) > 0);
    TEST_ASSERT_NOT_NULL(std::strstr(out, "\"rides\":[]"));
}

void test_full_history_fits_in_buffer() {
    RideRecord rides[kMaxRidesPerPayload];
    for (size_t i = 0; i < kMaxRidesPerPayload; ++i) {
        rides[i] = testRide(static_cast<uint32_t>(i + 1));
        rides[i].recordedAt = 1756400000LL;
        rides[i].values.maxSpeedKmh = 199.9f;
    }
    char out[kMaxPayloadBytes];

    const size_t len = buildPayload(testDevice(), rides, kMaxRidesPerPayload, out, sizeof(out));

    TEST_ASSERT_TRUE_MESSAGE(len > 0, "Pelna historia musi sie miescic w buforze");
    TEST_ASSERT_TRUE(len < kMaxPayloadBytes);
}

/// Za maly bufor ma dac zero, a nie obcieta przesylke — serwer zapisalby
/// polowe rekordu jako prawidlowy przejazd.
void test_small_buffer_yields_nothing() {
    const RideRecord rides[] = {testRide(7)};
    char out[32];

    TEST_ASSERT_EQUAL_UINT32(0, buildPayload(testDevice(), rides, 1, out, sizeof(out)));
}

/// Bez numeru przejazdu nie ma klucza (device_id, seq) — wpis przepadlby cicho.
void test_ride_without_seq_is_rejected() {
    RideRecord ride = testRide(0);
    char out[kMaxPayloadBytes];

    TEST_ASSERT_EQUAL_UINT32(0, buildPayload(testDevice(), &ride, 1, out, sizeof(out)));
}

void test_too_many_rides_rejected() {
    RideRecord rides[kMaxRidesPerPayload + 1];
    for (size_t i = 0; i < kMaxRidesPerPayload + 1; ++i) {
        rides[i] = testRide(static_cast<uint32_t>(i + 1));
    }
    char out[kMaxPayloadBytes];

    TEST_ASSERT_EQUAL_UINT32(
        0, buildPayload(testDevice(), rides, kMaxRidesPerPayload + 1, out, sizeof(out)));
}

void test_unsafe_identifier_rejected() {
    DeviceIdentity device = testDevice();
    device.deviceId = "a1b2\"c3";
    const RideRecord rides[] = {testRide(7)};
    char out[kMaxPayloadBytes];

    TEST_ASSERT_EQUAL_UINT32(0, buildPayload(device, rides, 1, out, sizeof(out)));
}

void test_parse_accepted_through() {
    uint32_t seq = 0;

    TEST_ASSERT_TRUE(parseAcceptedThrough("{\"accepted_through\":12}", seq));
    TEST_ASSERT_EQUAL_UINT32(12, seq);

    TEST_ASSERT_TRUE(parseAcceptedThrough("{ \"accepted_through\" : 340 , \"x\":1 }", seq));
    TEST_ASSERT_EQUAL_UINT32(340, seq);
}

/// Odpowiedz bez potwierdzenia nie moze przesunac znacznika wyslania —
/// przejazdy zostaja w kolejce do nastepnej proby.
void test_parse_rejects_missing_or_invalid() {
    uint32_t seq = 99;

    TEST_ASSERT_FALSE(parseAcceptedThrough("{\"error\":\"bad token\"}", seq));
    TEST_ASSERT_FALSE(parseAcceptedThrough("{\"accepted_through\":null}", seq));
    TEST_ASSERT_FALSE(parseAcceptedThrough("{\"accepted_through\":99999999999}", seq));
    TEST_ASSERT_FALSE(parseAcceptedThrough(nullptr, seq));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(99, seq, "Nieudany odczyt nie rusza wyniku");
}

// ── Ustawienia integracji ───────────────────────────────────────────────────

void test_config_starts_empty() {
    IntegrationConfig config;

    TEST_ASSERT_FALSE(config.hasNetwork());
    TEST_ASSERT_FALSE(config.hasToken());
    TEST_ASSERT_FALSE(config.isComplete());
}

void test_config_accepts_network_and_token() {
    IntegrationConfig config;

    TEST_ASSERT_TRUE(setSsid(config, "Dom Kowalskich"));
    TEST_ASSERT_TRUE(setPassword(config, "tajne haslo 123"));
    TEST_ASSERT_TRUE(setToken(config, "17|abcdefghijklmnop"));

    TEST_ASSERT_EQUAL_STRING("Dom Kowalskich", config.ssid);
    TEST_ASSERT_TRUE(config.isComplete());
}

/// Siec otwarta nie ma hasla — to poprawna konfiguracja, nie brak danych.
void test_empty_password_is_allowed() {
    IntegrationConfig config;
    setSsid(config, "HotelWiFi");

    TEST_ASSERT_TRUE(setPassword(config, ""));
    TEST_ASSERT_TRUE(config.hasNetwork());
}

void test_empty_ssid_and_token_are_rejected() {
    IntegrationConfig config;

    TEST_ASSERT_FALSE(setSsid(config, ""));
    TEST_ASSERT_FALSE(setToken(config, ""));
    TEST_ASSERT_FALSE(config.isComplete());
}

/// Polowa hasla jest gorsza niz jego brak: wyglada jak konfiguracja,
/// a nigdy sie nie polaczy.
void test_too_long_value_is_rejected_not_truncated() {
    IntegrationConfig config;
    setSsid(config, "DobraSiec");

    char tooLong[kMaxSsidLen + 10];
    for (size_t i = 0; i < sizeof(tooLong) - 1; ++i) tooLong[i] = 'x';
    tooLong[sizeof(tooLong) - 1] = '\0';

    TEST_ASSERT_FALSE(setSsid(config, tooLong));
    TEST_ASSERT_EQUAL_STRING_MESSAGE("DobraSiec", config.ssid,
                                     "Odrzucona wartosc nie moze nadpisac poprzedniej");
}

void test_exact_length_limit_fits() {
    IntegrationConfig config;

    char exact[kMaxSsidLen + 1];
    for (size_t i = 0; i < kMaxSsidLen; ++i) exact[i] = 'x';
    exact[kMaxSsidLen] = '\0';

    TEST_ASSERT_TRUE(setSsid(config, exact));
}

/// Odstep w tokenie to zawsze skutek zlego kopiowania ze strony.
void test_token_with_whitespace_is_rejected() {
    IntegrationConfig config;

    TEST_ASSERT_FALSE(setToken(config, "17|abc def"));
    TEST_ASSERT_FALSE(setToken(config, "17|abc\n"));
    TEST_ASSERT_FALSE(config.hasToken());
}

void test_masked_token_shows_only_last_four() {
    IntegrationConfig config;
    char mask[24];

    maskToken(config, mask, sizeof(mask));
    TEST_ASSERT_EQUAL_STRING("BRAK", mask);

    setToken(config, "17|abcdefghijklmnwxyz");
    maskToken(config, mask, sizeof(mask));
    TEST_ASSERT_EQUAL_STRING("****wxyz", mask);

    // Token podejrzanie krotki: nie odslaniamy polowy sekretu.
    setToken(config, "abcdef");
    maskToken(config, mask, sizeof(mask));
    TEST_ASSERT_EQUAL_STRING("****", mask);
}

// ── Komendy konfiguracji przez USB ─────────────────────────────────────────

void test_command_sets_every_field() {
    IntegrationConfig config;

    ConfigCommandResult result = applyConfigLine(config, "SIEC=Dom Kowalskich");
    TEST_ASSERT_TRUE(result.field == ConfigField::Ssid);
    TEST_ASSERT_TRUE(result.accepted);
    TEST_ASSERT_TRUE(result.needsSave);
    TEST_ASSERT_EQUAL_STRING("Dom Kowalskich", config.ssid);

    TEST_ASSERT_TRUE(applyConfigLine(config, "HASLO=tajne haslo").accepted);
    TEST_ASSERT_EQUAL_STRING("tajne haslo", config.password);

    TEST_ASSERT_TRUE(applyConfigLine(config, "TOKEN=17|abcdefghij").accepted);
    TEST_ASSERT_EQUAL_STRING("17|abcdefghij", config.token);
    TEST_ASSERT_TRUE(config.isComplete());
}

/// Uzytkownik pisze w monitorze portu, nie w jezyku programowania.
void test_command_ignores_letter_case_and_key_spacing() {
    IntegrationConfig config;

    TEST_ASSERT_TRUE(applyConfigLine(config, "siec=Dom").accepted);
    TEST_ASSERT_TRUE(applyConfigLine(config, " Token =17|abcdefghij").accepted);
    TEST_ASSERT_EQUAL_STRING("Dom", config.ssid);
    TEST_ASSERT_EQUAL_STRING("17|abcdefghij", config.token);

    // Alias dla osob przyzwyczajonych do nazewnictwa WiFi.
    TEST_ASSERT_TRUE(applyConfigLine(config, "SSID=Inna").accepted);
    TEST_ASSERT_EQUAL_STRING("Inna", config.ssid);
}

/// Wartosc idzie doslownie — takze znak "=", ktory potrafi wystapic w tokenie.
void test_command_takes_value_verbatim() {
    IntegrationConfig config;

    TEST_ASSERT_TRUE(applyConfigLine(config, "TOKEN=17|abc=def==").accepted);
    TEST_ASSERT_EQUAL_STRING("17|abc=def==", config.token);

    // Odstep za "=" jest czescia hasla, nie ozdoba skladni.
    TEST_ASSERT_TRUE(applyConfigLine(config, "HASLO= zaczyna sie spacja").accepted);
    TEST_ASSERT_EQUAL_STRING(" zaczyna sie spacja", config.password);
}

/// Odrzucona wartosc NIE moze zepsuc tego, co juz dziala.
void test_command_rejects_bad_value_without_touching_config() {
    IntegrationConfig config;
    applyConfigLine(config, "TOKEN=17|dobrytoken");

    const ConfigCommandResult result = applyConfigLine(config, "TOKEN=17|zly token");
    TEST_ASSERT_TRUE(result.field == ConfigField::Token);
    TEST_ASSERT_FALSE(result.accepted);
    TEST_ASSERT_FALSE(result.needsSave);
    TEST_ASSERT_EQUAL_STRING("17|dobrytoken", config.token);

    // Pusta siec to nie jest "wyczyszczenie sieci" — do tego jest KASUJ.
    applyConfigLine(config, "SIEC=Dom");
    TEST_ASSERT_FALSE(applyConfigLine(config, "SIEC=").accepted);
    TEST_ASSERT_EQUAL_STRING("Dom", config.ssid);
}

/// Puste haslo jest poprawna wartoscia — siec otwarta.
void test_command_accepts_empty_password() {
    IntegrationConfig config;
    applyConfigLine(config, "HASLO=cokolwiek");

    TEST_ASSERT_TRUE(applyConfigLine(config, "HASLO=").accepted);
    TEST_ASSERT_EQUAL_STRING("", config.password);
}

void test_command_status_and_clear() {
    IntegrationConfig config;
    applyConfigLine(config, "SIEC=Dom");
    applyConfigLine(config, "TOKEN=17|abcdefghij");

    // STAN niczego nie zmienia i nie wymaga zapisu.
    const ConfigCommandResult status = applyConfigLine(config, "stan");
    TEST_ASSERT_TRUE(status.field == ConfigField::Status);
    TEST_ASSERT_TRUE(status.accepted);
    TEST_ASSERT_FALSE(status.needsSave);
    TEST_ASSERT_TRUE(config.hasToken());

    const ConfigCommandResult cleared = applyConfigLine(config, "KASUJ");
    TEST_ASSERT_TRUE(cleared.field == ConfigField::Clear);
    TEST_ASSERT_TRUE(cleared.needsSave);
    TEST_ASSERT_FALSE(config.hasNetwork());
    TEST_ASSERT_FALSE(config.hasToken());
}

/// Linie rejestratora (L, D<nr>, X) i smieci maja przejsc bokiem.
void test_command_leaves_foreign_lines_alone() {
    IntegrationConfig config;
    applyConfigLine(config, "SIEC=Dom");

    const char* foreign[] = {"", "L", "D3", "X", "cokolwiek", "=bez klucza"};
    for (const char* line : foreign) {
        const ConfigCommandResult result = applyConfigLine(config, line);
        TEST_ASSERT_TRUE(result.field == ConfigField::None);
        TEST_ASSERT_FALSE(result.needsSave);
    }
    TEST_ASSERT_EQUAL_STRING("Dom", config.ssid);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_payload_without_gps_sends_nulls);
    RUN_TEST(test_payload_with_gps_fills_time_and_speed);
    RUN_TEST(test_payload_keeps_ride_order);
    RUN_TEST(test_empty_payload_is_valid_json);
    RUN_TEST(test_full_history_fits_in_buffer);
    RUN_TEST(test_small_buffer_yields_nothing);
    RUN_TEST(test_ride_without_seq_is_rejected);
    RUN_TEST(test_too_many_rides_rejected);
    RUN_TEST(test_unsafe_identifier_rejected);
    RUN_TEST(test_parse_accepted_through);
    RUN_TEST(test_parse_rejects_missing_or_invalid);

    RUN_TEST(test_config_starts_empty);
    RUN_TEST(test_config_accepts_network_and_token);
    RUN_TEST(test_empty_password_is_allowed);
    RUN_TEST(test_empty_ssid_and_token_are_rejected);
    RUN_TEST(test_too_long_value_is_rejected_not_truncated);
    RUN_TEST(test_exact_length_limit_fits);
    RUN_TEST(test_token_with_whitespace_is_rejected);
    RUN_TEST(test_masked_token_shows_only_last_four);

    RUN_TEST(test_command_sets_every_field);
    RUN_TEST(test_command_ignores_letter_case_and_key_spacing);
    RUN_TEST(test_command_takes_value_verbatim);
    RUN_TEST(test_command_rejects_bad_value_without_touching_config);
    RUN_TEST(test_command_accepts_empty_password);
    RUN_TEST(test_command_status_and_clear);
    RUN_TEST(test_command_leaves_foreign_lines_alone);
    return UNITY_END();
}
