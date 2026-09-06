#include "RideNoise.h"

#include <cmath>

#include "SustainedLevel.h"

namespace noise {
namespace {

uint16_t saturate(uint32_t value) {
    return value > 65535u ? static_cast<uint16_t>(65535) : static_cast<uint16_t>(value);
}

}  // namespace

void RideNoise::raiseTo(const RideNoise& other) {
    if (other.measured() && (!measured() || other.maxDb10 > maxDb10)) {
        maxDb10 = other.maxDb10;
        // Predkosc idzie RAZEM z poziomem, nie osobno: para (ile, przy jakiej
        // predkosci) ma opisywac jedna chwile, inaczej nie da sie z niej
        // niczego wywnioskowac.
        atSpeedKmh = other.atSpeedKmh;
    }
    if (other.clipped > clipped) clipped = other.clipped;
    if (other.dropped > dropped) dropped = other.dropped;
    if (other.calibration != 0) calibration = other.calibration;
}

RideNoise makeRideNoise(float maxDb, float speedKmh, uint32_t clipped, uint32_t dropped,
                        uint8_t calibration) {
    RideNoise out;
    out.calibration = calibration;
    out.clipped = saturate(clipped);
    out.dropped = saturate(dropped);

    if (maxDb == SustainedLevel::kNoData || !(maxDb == maxDb)) return out;

    const float scaled = maxDb * 10.0f;
    // Zaokraglenie od zera, zeby -0,05 dB nie stalo sie zerem tylko dlatego,
    // ze jest ujemne.
    const float rounded = scaled >= 0.0f ? std::floor(scaled + 0.5f) : std::ceil(scaled - 0.5f);

    // Nasycenie zamiast przewiniecia: pomiar poza zakresem ma wygladac jak
    // skraj skali, a nie jak przypadkowa liczba o przeciwnym znaku.
    if (rounded >= 32767.0f) {
        out.maxDb10 = 32767;
    } else if (rounded <= -32767.0f) {
        out.maxDb10 = -32767;  // NIE kNoValue: to jest pomiar, nie jego brak
    } else {
        out.maxDb10 = static_cast<int16_t>(rounded);
    }

    if (speedKmh > 0.0f) {
        const float speed = std::floor(speedKmh + 0.5f);
        out.atSpeedKmh = speed >= 65535.0f ? static_cast<uint16_t>(65535)
                                           : static_cast<uint16_t>(speed);
    }
    return out;
}

void packRideNoise(const RideNoise& value, uint8_t* out) {
    const uint16_t level = static_cast<uint16_t>(value.maxDb10);
    out[0] = static_cast<uint8_t>(level & 0xFF);
    out[1] = static_cast<uint8_t>((level >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>(value.atSpeedKmh & 0xFF);
    out[3] = static_cast<uint8_t>((value.atSpeedKmh >> 8) & 0xFF);
    out[4] = static_cast<uint8_t>(value.clipped & 0xFF);
    out[5] = static_cast<uint8_t>((value.clipped >> 8) & 0xFF);
    out[6] = static_cast<uint8_t>(value.dropped & 0xFF);
    out[7] = static_cast<uint8_t>((value.dropped >> 8) & 0xFF);
    out[8] = value.calibration;
}

void unpackRideNoise(const uint8_t* in, RideNoise& out) {
    const uint16_t level = static_cast<uint16_t>(in[0]) |
                           static_cast<uint16_t>(static_cast<uint16_t>(in[1]) << 8);
    out.maxDb10 = static_cast<int16_t>(level);
    out.atSpeedKmh = static_cast<uint16_t>(in[2]) |
                     static_cast<uint16_t>(static_cast<uint16_t>(in[3]) << 8);
    out.clipped = static_cast<uint16_t>(in[4]) |
                  static_cast<uint16_t>(static_cast<uint16_t>(in[5]) << 8);
    out.dropped = static_cast<uint16_t>(in[6]) |
                  static_cast<uint16_t>(static_cast<uint16_t>(in[7]) << 8);
    out.calibration = in[8];
}

}  // namespace noise
