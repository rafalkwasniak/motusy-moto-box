#include "Store.h"

#include <cstring>

namespace hal {
namespace {

constexpr const char* kNamespace = "motobox";

constexpr const char* kKeyVersion = "ver";
constexpr const char* kKeyOverall = "max";
constexpr const char* kKeyRide = "ride";
constexpr const char* kKeyAlarm = "alarm";
constexpr const char* kKeyMount = "mount";

/// Piec wartosci rekordowych, kolejnosc utrwalona przez kSchemaVersion.
constexpr size_t kRideValuesBytes = 5 * sizeof(float);
/// Flaga "skalibrowano" + dziewiec elementow macierzy obrotu.
constexpr size_t kMountBytes = 1 + 9 * sizeof(float);

void packFloat(uint8_t*& cursor, float value) {
    std::memcpy(cursor, &value, sizeof(float));
    cursor += sizeof(float);
}

float unpackFloat(const uint8_t*& cursor) {
    float value = 0.0f;
    std::memcpy(&value, cursor, sizeof(float));
    cursor += sizeof(float);
    return value;
}

void packRideValues(const motion::RideValues& values, uint8_t* out) {
    uint8_t* cursor = out;
    packFloat(cursor, values.maxLeanLeftDeg);
    packFloat(cursor, values.maxLeanRightDeg);
    packFloat(cursor, values.maxAccelG);
    packFloat(cursor, values.maxBrakeG);
    packFloat(cursor, values.maxSpeedKmh);
}

void unpackRideValues(const uint8_t* in, motion::RideValues& values) {
    const uint8_t* cursor = in;
    values.maxLeanLeftDeg = unpackFloat(cursor);
    values.maxLeanRightDeg = unpackFloat(cursor);
    values.maxAccelG = unpackFloat(cursor);
    values.maxBrakeG = unpackFloat(cursor);
    values.maxSpeedKmh = unpackFloat(cursor);
}

void packVec3(uint8_t*& cursor, const motion::Vec3& v) {
    packFloat(cursor, v.x);
    packFloat(cursor, v.y);
    packFloat(cursor, v.z);
}

motion::Vec3 unpackVec3(const uint8_t*& cursor) {
    const float x = unpackFloat(cursor);
    const float y = unpackFloat(cursor);
    const float z = unpackFloat(cursor);
    return {x, y, z};
}

}  // namespace

bool Store::begin() {
    available_ = prefs_.begin(kNamespace, false);
    return available_;
}

LoadResult Store::load(PersistentState& out) {
    if (!available_) return LoadResult::Failed;

    if (!prefs_.isKey(kKeyVersion)) return LoadResult::Fresh;

    const uint32_t version = prefs_.getUInt(kKeyVersion, 0);
    if (version != kSchemaVersion) {
        // Format sie zmienil. Lepiej zaczac od zera niz zinterpretowac stare
        // bajty jako nowe pola i pokazac uzytkownikowi wymyslone rekordy.
        prefs_.clear();
        return LoadResult::Migrated;
    }

    uint8_t buffer[kMountBytes];

    if (prefs_.getBytes(kKeyOverall, buffer, kRideValuesBytes) == kRideValuesBytes) {
        unpackRideValues(buffer, out.overall);
    }
    if (prefs_.getBytes(kKeyRide, buffer, kRideValuesBytes) == kRideValuesBytes) {
        unpackRideValues(buffer, out.ride);
    }

    out.alarmEnabled = prefs_.getUChar(kKeyAlarm, 1) != 0;

    if (prefs_.getBytes(kKeyMount, buffer, kMountBytes) == kMountBytes) {
        const uint8_t* cursor = buffer;
        out.mountCalibrated = (*cursor++) != 0;
        out.mountRotation.row0 = unpackVec3(cursor);
        out.mountRotation.row1 = unpackVec3(cursor);
        out.mountRotation.row2 = unpackVec3(cursor);
    }

    return LoadResult::Restored;
}

bool Store::saveResults(const motion::RideValues& overall, const motion::RideValues& ride) {
    if (!available_) return false;

    uint8_t buffer[kRideValuesBytes];

    packRideValues(overall, buffer);
    if (prefs_.putBytes(kKeyOverall, buffer, kRideValuesBytes) != kRideValuesBytes) return false;

    packRideValues(ride, buffer);
    if (prefs_.putBytes(kKeyRide, buffer, kRideValuesBytes) != kRideValuesBytes) return false;

    prefs_.putUInt(kKeyVersion, kSchemaVersion);
    return true;
}

bool Store::saveAlarmEnabled(bool enabled) {
    if (!available_) return false;
    prefs_.putUChar(kKeyAlarm, enabled ? 1 : 0);
    prefs_.putUInt(kKeyVersion, kSchemaVersion);
    return true;
}

bool Store::saveMount(const motion::MountCalibration& mount) {
    if (!available_) return false;

    uint8_t buffer[kMountBytes];
    uint8_t* cursor = buffer;
    *cursor++ = mount.isCalibrated() ? 1 : 0;
    packVec3(cursor, mount.rotation().row0);
    packVec3(cursor, mount.rotation().row1);
    packVec3(cursor, mount.rotation().row2);

    if (prefs_.putBytes(kKeyMount, buffer, kMountBytes) != kMountBytes) return false;
    prefs_.putUInt(kKeyVersion, kSchemaVersion);
    return true;
}

bool Store::clearAll() {
    if (!available_) return false;
    return prefs_.clear();
}

}  // namespace hal
