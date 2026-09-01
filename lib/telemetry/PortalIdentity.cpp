#include "PortalIdentity.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace telemetry {
namespace {

/// Alfabet bez znakow mylacych sie przy przepisywaniu z ekranu:
/// nie ma O ani 0, nie ma I ani 1. 32 znaki = dokladnie 5 bitow na znak.
constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
constexpr size_t kAlphabetSize = sizeof(kAlphabet) - 1;
static_assert(kAlphabetSize == 32, "alfabet musi miec 32 znaki");

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

/// FNV-1a. Nie jest to funkcja kryptograficzna i nie musi nia byc — haslo
/// chroni okno czasowe, nie tajnosc algorytmu (patrz naglowek). Potrzebujemy
/// tylko powtarzalnego rozrzucenia znakow device_id po calym alfabecie.
uint64_t hash64(const char* text, uint64_t seed) {
    uint64_t h = seed;
    for (const char* p = text; *p != '\0'; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= 1099511628211ULL;
    }
    return h;
}

}  // namespace

void portalSsid(const char* deviceId, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;

    const char* id = deviceId != nullptr ? deviceId : "";
    const size_t len = std::strlen(id);

    // Urzadzenie bez odczytanego identyfikatora nadal musi dac sie skonfigurowac,
    // wiec nazwa istnieje zawsze — po prostu bez czesci rozrozniajacej.
    char suffix[5] = "0000";
    if (len >= 4) {
        for (size_t i = 0; i < 4; ++i) suffix[i] = upper(id[len - 4 + i]);
    }

    std::snprintf(out, outSize, "MOTOBOX-%s", suffix);
}

void portalPassword(const char* deviceId, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return;

    const char* id = deviceId != nullptr ? deviceId : "";

    // Dwa niezalezne przebiegi hasha: jeden 64-bitowy daje 12 znakow po 5 bitow,
    // ale ostatnie z nich sa silnie skorelowane z pierwszymi. Drugi zasiew
    // rozdziela polowy hasla.
    const uint64_t h1 = hash64(id, 1469598103934665603ULL);
    const uint64_t h2 = hash64(id, h1 ^ 0x9E3779B97F4A7C15ULL);

    char password[kPortalPasswordLen + 1];
    for (size_t i = 0; i < kPortalPasswordLen; ++i) {
        const uint64_t source = (i < kPortalPasswordLen / 2) ? h1 : h2;
        const unsigned shift = static_cast<unsigned>((i % (kPortalPasswordLen / 2)) * 5);
        password[i] = kAlphabet[(source >> shift) & 0x1F];
    }
    password[kPortalPasswordLen] = '\0';

    std::snprintf(out, outSize, "%s", password);
}

}  // namespace telemetry
