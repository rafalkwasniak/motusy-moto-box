// Motusy Moto Box — testy formatu przesylki telemetrycznej.
//
// Ten format jest kontraktem z API po stronie serwera, wiec testy sprawdzaja
// doslowna tresc, a nie tylko "czy sie da sparsowac". Zmiana ksztaltu JSON-a
// ma tu psuc testy — to jedyny sygnal, ze trzeba ruszyc takze druga strone.

#include <unity.h>

#include <cstring>

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
    return UNITY_END();
}
