#include "TelemetryJson.h"

#include <cstdio>
#include <cstring>

namespace telemetry {
namespace {

/// Najdluzszy dopuszczalny identyfikator (device_id, wersja firmware).
constexpr size_t kMaxIdentifierLen = 64;

/// Dopisywanie do bufora wolajacego z pilnowaniem konca.
///
/// Po pierwszym przepelnieniu `ok` zostaje falszem i kazde kolejne dopisanie
/// jest pomijane — dzieki temu nie trzeba sprawdzac wyniku po kazdym polu,
/// wystarczy jedno sprawdzenie na koncu.
class Writer {
public:
    Writer(char* out, size_t size) : out_(out), size_(size) {
        if (size_ == 0) ok_ = false;
    }

    void text(const char* s) {
        if (!ok_) return;
        const size_t len = std::strlen(s);
        // Miejsce na tekst i na konczace zero.
        if (pos_ + len + 1 > size_) {
            ok_ = false;
            return;
        }
        std::memcpy(out_ + pos_, s, len);
        pos_ += len;
    }

    void uint32(uint32_t value) {
        char tmp[12];
        std::snprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(value));
        text(tmp);
    }

    void int64(long long value) {
        char tmp[24];
        std::snprintf(tmp, sizeof(tmp), "%lld", value);
        text(tmp);
    }

    /// Liczba z ustalona liczba miejsc po przecinku. Stala precyzja zamiast
    /// "%g" trzyma szum zmiennoprzecinkowy z dala od bazy: 42.29999923706055
    /// w kolumnie "maksymalny przechyl" wygladaloby jak blad pomiaru.
    void number(float value, int decimals) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "%.*f", decimals, static_cast<double>(value));
        text(tmp);
    }

    /// Pomiar, ktorego urzadzenie nie zna, idzie jako null — nie jako zero.
    /// Prosto z ekranu: bez GPS predkosc pokazuje "---", a nie "0 km/h".
    void numberOrNull(float value, int decimals) {
        if (value <= 0.0f) {
            text("null");
            return;
        }
        number(value, decimals);
    }

    bool ok() const { return ok_; }

    size_t finish() {
        if (!ok_) return 0;
        out_[pos_] = '\0';
        return pos_;
    }

private:
    char* out_;
    size_t size_;
    size_t pos_ = 0;
    bool ok_ = true;
};

/// Identyfikatory sa nasze i skladaja sie ze znakow bezpiecznych w JSON.
/// Zamiast implementowac escapowanie, ktore nigdy nie bedzie potrzebne,
/// odrzucamy przesylke — falszywy identyfikator to blad w kodzie, nie dane
/// od uzytkownika.
bool isSafeIdentifier(const char* s) {
    if (s == nullptr) return false;
    const size_t len = std::strlen(s);
    if (len == 0 || len > kMaxIdentifierLen) return false;

    for (size_t i = 0; i < len; ++i) {
        const char c = s[i];
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!allowed) return false;
    }
    return true;
}

void writeRide(Writer& w, const RideRecord& ride) {
    w.text("{\"seq\":");
    w.uint32(ride.seq);

    w.text(",\"recorded_at\":");
    if (ride.recordedAt > 0) {
        w.int64(ride.recordedAt);
    } else {
        w.text("null");
    }

    w.text(",\"duration_s\":");
    w.uint32(ride.durationS);

    w.text(",\"lean_left_deg\":");
    w.number(ride.values.maxLeanLeftDeg, 1);
    w.text(",\"lean_right_deg\":");
    w.number(ride.values.maxLeanRightDeg, 1);
    w.text(",\"accel_g\":");
    w.number(ride.values.maxAccelG, 2);
    w.text(",\"brake_g\":");
    w.number(ride.values.maxBrakeG, 2);
    w.text(",\"speed_kmh\":");
    w.numberOrNull(ride.values.maxSpeedKmh, 1);

    w.text("}");
}

}  // namespace

size_t buildPayload(const DeviceIdentity& device, const RideRecord* rides, size_t count,
                    char* out, size_t outSize) {
    if (out == nullptr) return 0;
    if (count > kMaxRidesPerPayload) return 0;
    if (count > 0 && rides == nullptr) return 0;
    if (!isSafeIdentifier(device.deviceId) || !isSafeIdentifier(device.firmware)) return 0;

    // Rekord bez numeru nie ma jak trafic w klucz (device_id, seq) po stronie
    // API — wysylka takiego zestawu skonczylaby sie cicha strata przejazdu.
    for (size_t i = 0; i < count; ++i) {
        if (rides[i].seq == 0) return 0;
    }

    Writer w(out, outSize);

    w.text("{\"device_id\":\"");
    w.text(device.deviceId);
    w.text("\",\"fw\":\"");
    w.text(device.firmware);
    w.text("\",\"calibrated\":");
    w.text(device.calibrated ? "true" : "false");
    w.text(",\"rides\":[");

    for (size_t i = 0; i < count; ++i) {
        if (i > 0) w.text(",");
        writeRide(w, rides[i]);
    }

    w.text("]}");

    return w.finish();
}

bool parseAcceptedThrough(const char* json, uint32_t& seqOut) {
    if (json == nullptr) return false;

    static const char kKey[] = "accepted_through";
    const char* p = std::strstr(json, kKey);
    if (p == nullptr) return false;
    p += sizeof(kKey) - 1;

    // Przeskok przez cudzyslow zamykajacy nazwe pola, dwukropek i odstepy.
    while (*p == '"' || *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;

    if (*p < '0' || *p > '9') return false;

    // Wlasne skladanie liczby zamiast strtoul: chcemy odrzucic wartosc, ktora
    // nie miesci sie w numerze przejazdu, a nie dostac obcieta.
    unsigned long long value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + static_cast<unsigned>(*p - '0');
        if (value > 0xFFFFFFFFull) return false;
        ++p;
    }

    seqOut = static_cast<uint32_t>(value);
    return true;
}

}  // namespace telemetry
