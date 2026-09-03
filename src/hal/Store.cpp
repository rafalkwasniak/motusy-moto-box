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
constexpr const char* kKeyArchived = "arch";
constexpr const char* kKeyHistory = "hist";
constexpr const char* kKeyRideSeq = "seq";
constexpr const char* kKeySent = "sent";
constexpr const char* kKeyRideDuration = "rdur";
constexpr const char* kKeyHistoryDuration = "hdur";
constexpr const char* kKeyHistoryTime = "hts";
constexpr const char* kKeySsid = "ssid";
constexpr const char* kKeyPassword = "pass";
constexpr const char* kKeyToken = "tok";

/// Piec wartosci rekordowych, kolejnosc utrwalona przez kSchemaVersion.
constexpr size_t kRideValuesBytes = 5 * sizeof(float);
/// Flaga "skalibrowano" + dziewiec elementow macierzy obrotu.
constexpr size_t kMountBytes = 1 + 9 * sizeof(float);
/// Licznik wpisow + pelna pojemnosc historii.
constexpr size_t kHistoryBytes =
    1 + motion::RideHistory::kCapacity * kRideValuesBytes;
/// Czasy trwania przejazdow z historii — osobny klucz, rownolegly do niej.
/// Osobny, bo dolozenie ich do `kHistoryBytes` zmienialoby uklad istniejacego
/// wpisu i wymagaloby podniesienia wersji schematu.
constexpr size_t kHistoryDurationBytes =
    motion::RideHistory::kCapacity * sizeof(uint32_t);
/// Znaczniki czasu przejazdow z historii — kolejny klucz rownolegly, z tego
/// samego powodu co czasy trwania.
constexpr size_t kHistoryTimeBytes = motion::RideHistory::kCapacity * sizeof(uint32_t);

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

void writeIntegration(Preferences& prefs, const telemetry::IntegrationConfig& integration) {
    prefs.putString(kKeySsid, integration.ssid);
    prefs.putString(kKeyPassword, integration.password);
    prefs.putString(kKeyToken, integration.token);
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
        //
        // NUMERACJA PRZEJAZDOW PRZEZYWA ZMIANE FORMATU. Numery sa kluczem po
        // stronie API — gdyby wrocily do jedynki, kolejne przejazdy nadpisalyby
        // te, ktore juz sa na koncie. Historia przepada, wiec nie ma juz czego
        // wysylac: znacznik wyslania dogania numeracje.
        // USTAWIENIA INTEGRACJI TEZ PRZEZYWAJA. Sa przepisywane recznie
        // z telefonu — kasowanie ich przy kazdej zmianie formatu wynikow
        // byloby karaniem uzytkownika za aktualizacje firmware.
        const uint32_t lastSeq = prefs_.getUInt(kKeyRideSeq, 0);
        telemetry::IntegrationConfig integration;
        prefs_.getString(kKeySsid, integration.ssid, sizeof(integration.ssid));
        prefs_.getString(kKeyPassword, integration.password, sizeof(integration.password));
        prefs_.getString(kKeyToken, integration.token, sizeof(integration.token));

        prefs_.clear();

        if (lastSeq > 0) {
            prefs_.putUInt(kKeyRideSeq, lastSeq);
            prefs_.putUInt(kKeySent, lastSeq);
            prefs_.putUInt(kKeyVersion, kSchemaVersion);
        }
        if (integration.hasNetwork() || integration.hasToken()) {
            writeIntegration(prefs_, integration);
            prefs_.putUInt(kKeyVersion, kSchemaVersion);
        }

        out.lastRideSeq = lastSeq;
        out.sentThrough = lastSeq;
        out.integration = integration;
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
    out.rideArchived = prefs_.getUChar(kKeyArchived, 0) != 0;

    // Klucze dolozone po v2. Brak wpisu (pamiec sprzed tej wersji firmware)
    // znaczy "zadnego przejazdu jeszcze nie ponumerowano".
    out.lastRideSeq = prefs_.getUInt(kKeyRideSeq, 0);
    out.sentThrough = prefs_.getUInt(kKeySent, 0);
    out.rideDurationS = prefs_.getUInt(kKeyRideDuration, 0);

    prefs_.getString(kKeySsid, out.integration.ssid, sizeof(out.integration.ssid));
    prefs_.getString(kKeyPassword, out.integration.password, sizeof(out.integration.password));
    prefs_.getString(kKeyToken, out.integration.token, sizeof(out.integration.token));

    {
        uint8_t historyBuffer[kHistoryBytes];
        if (prefs_.getBytes(kKeyHistory, historyBuffer, kHistoryBytes) == kHistoryBytes) {
            const uint8_t* cursor = historyBuffer;
            size_t count = *cursor++;
            if (count > motion::RideHistory::kCapacity) count = motion::RideHistory::kCapacity;
            motion::RideValues rides[motion::RideHistory::kCapacity];
            for (size_t i = 0; i < motion::RideHistory::kCapacity; ++i) {
                unpackRideValues(cursor, rides[i]);
                cursor += kRideValuesBytes;
            }

            // Klucz dolozony pozniej niz sama historia. Jego brak znaczy
            // "przejazdy sprzed pomiaru czasu" — ida z czasem zerowym.
            uint32_t durations[motion::RideHistory::kCapacity] = {};
            const bool haveDurations =
                prefs_.getBytes(kKeyHistoryDuration, durations, kHistoryDurationBytes) ==
                kHistoryDurationBytes;

            // To samo dotyczy znacznikow czasu: przejazdy sprzed modulu GPS
            // ida z zerem, czyli beda wyslane z `recorded_at: null`.
            uint32_t timestamps[motion::RideHistory::kCapacity] = {};
            const bool haveTimestamps =
                prefs_.getBytes(kKeyHistoryTime, timestamps, kHistoryTimeBytes) ==
                kHistoryTimeBytes;

            out.history.restore(rides, haveDurations ? durations : nullptr, count,
                                haveTimestamps ? timestamps : nullptr);
        }
    }

    if (prefs_.getBytes(kKeyMount, buffer, kMountBytes) == kMountBytes) {
        const uint8_t* cursor = buffer;
        out.mountCalibrated = (*cursor++) != 0;
        out.mountRotation.row0 = unpackVec3(cursor);
        out.mountRotation.row1 = unpackVec3(cursor);
        out.mountRotation.row2 = unpackVec3(cursor);
    }

    return LoadResult::Restored;
}

bool Store::saveResults(const motion::RideValues& overall, const motion::RideValues& ride,
                        bool rideArchived, uint32_t rideDurationS) {
    if (!available_) return false;

    uint8_t buffer[kRideValuesBytes];

    packRideValues(overall, buffer);
    if (prefs_.putBytes(kKeyOverall, buffer, kRideValuesBytes) != kRideValuesBytes) return false;

    packRideValues(ride, buffer);
    if (prefs_.putBytes(kKeyRide, buffer, kRideValuesBytes) != kRideValuesBytes) return false;

    prefs_.putUInt(kKeyRideDuration, rideDurationS);
    prefs_.putUChar(kKeyArchived, rideArchived ? 1 : 0);
    prefs_.putUInt(kKeyVersion, kSchemaVersion);
    return true;
}

bool Store::saveHistory(const motion::RideHistory& history) {
    if (!available_) return false;

    uint8_t buffer[kHistoryBytes];
    uint8_t* cursor = buffer;
    *cursor++ = static_cast<uint8_t>(history.count());
    for (size_t i = 0; i < motion::RideHistory::kCapacity; ++i) {
        packRideValues(history.at(i), cursor);
        cursor += kRideValuesBytes;
    }

    if (prefs_.putBytes(kKeyHistory, buffer, kHistoryBytes) != kHistoryBytes) return false;

    uint32_t durations[motion::RideHistory::kCapacity] = {};
    for (size_t i = 0; i < motion::RideHistory::kCapacity; ++i) {
        durations[i] = history.durationAt(i);
    }
    if (prefs_.putBytes(kKeyHistoryDuration, durations, kHistoryDurationBytes) !=
        kHistoryDurationBytes) {
        return false;
    }

    uint32_t timestamps[motion::RideHistory::kCapacity] = {};
    for (size_t i = 0; i < motion::RideHistory::kCapacity; ++i) {
        timestamps[i] = history.recordedAtAt(i);
    }
    if (prefs_.putBytes(kKeyHistoryTime, timestamps, kHistoryTimeBytes) != kHistoryTimeBytes) {
        return false;
    }

    prefs_.putUInt(kKeyVersion, kSchemaVersion);
    return true;
}

bool Store::saveUploadState(uint32_t lastRideSeq, uint32_t sentThrough) {
    if (!available_) return false;
    prefs_.putUInt(kKeyRideSeq, lastRideSeq);
    prefs_.putUInt(kKeySent, sentThrough);
    prefs_.putUInt(kKeyVersion, kSchemaVersion);
    return true;
}

bool Store::saveIntegration(const telemetry::IntegrationConfig& integration) {
    if (!available_) return false;
    writeIntegration(prefs_, integration);
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
