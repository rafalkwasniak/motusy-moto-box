#include "IntegrationConfig.h"

#include <cstdio>
#include <cstring>

namespace telemetry {
namespace {

/// SSID i haslo moga zawierac niemal wszystko, ale znaki sterujace w nich nie
/// wystapia — a gdyby wystapily, byloby to uszkodzone dane z formularza.
bool isPrintable(const char* value, bool allowSpaces) {
    for (const char* p = value; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c < 0x20 || c == 0x7F) return false;
        if (!allowSpaces && c == ' ') return false;
    }
    return true;
}

bool assign(char* field, size_t capacity, const char* value, bool allowEmpty, bool allowSpaces) {
    if (value == nullptr) return false;

    const size_t len = std::strlen(value);
    if (len == 0 && !allowEmpty) return false;
    if (len > capacity) return false;
    if (!isPrintable(value, allowSpaces)) return false;

    std::memcpy(field, value, len);
    field[len] = '\0';
    return true;
}

}  // namespace

bool setSsid(IntegrationConfig& config, const char* value) {
    // Nazwy sieci bywaja z odstepami ("Dom Kowalskich").
    return assign(config.ssid, kMaxSsidLen, value, false, true);
}

bool setPassword(IntegrationConfig& config, const char* value) {
    // Puste haslo = siec otwarta.
    return assign(config.password, kMaxPasswordLen, value, true, true);
}

bool setToken(IntegrationConfig& config, const char* value) {
    // Odstep w tokenie to zawsze skutek zlego kopiowania — odrzucamy,
    // zamiast wysylac naglowek, ktory serwer i tak odrzuci.
    return assign(config.token, kMaxTokenLen, value, false, false);
}

void maskToken(const IntegrationConfig& config, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;

    const size_t len = std::strlen(config.token);
    if (len == 0) {
        std::snprintf(out, outSize, "BRAK");
        return;
    }

    // Token krotszy niz osiem znakow jest podejrzany sam w sobie — pokazujemy
    // wtedy same gwiazdki, zeby nie odslonic polowy sekretu.
    if (len < 8) {
        std::snprintf(out, outSize, "****");
        return;
    }

    std::snprintf(out, outSize, "****%s", config.token + len - 4);
}

}  // namespace telemetry
