// Motusy Moto Box — ustawienia integracji ze strona motobox.motusy.top.
//
// Trzy rzeczy wpisywane raz, przy uruchamianiu urzadzenia: nazwa domowej sieci,
// jej haslo i token konta przepisany ze strony.
//
// BUFORY O STALYM ROZMIARZE, nie std::string: te wartosci leza w pamieci przez
// cale zycie urzadzenia i trafiaja wprost do NVS. Limity sa takie same jak
// w standardzie WiFi, wiec dluzsze i tak nie zadzialaloby.
//
// TOKEN NIE JEST WALIDOWANY CO DO TRESCI — urzadzenie nie wie, jak wyglada
// prawidlowy token, wie tylko, ze skladaja sie na niego znaki drukowalne bez
// odstepow (format Laravel Sanctum: "17|abc..." tez sie miesci). Prawdziwym
// sprawdzeniem jest odpowiedz serwera na GET /api/v1/ping.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>

namespace telemetry {

/// Limity ze standardu WiFi.
constexpr size_t kMaxSsidLen = 32;
constexpr size_t kMaxPasswordLen = 63;
/// Zapas na tokeny dluzsze niz typowe.
constexpr size_t kMaxTokenLen = 128;

struct IntegrationConfig {
    char ssid[kMaxSsidLen + 1] = {};
    char password[kMaxPasswordLen + 1] = {};
    char token[kMaxTokenLen + 1] = {};

    /// Siec ustawiona. Puste haslo jest dopuszczalne — sieci otwarte istnieja.
    bool hasNetwork() const { return ssid[0] != '\0'; }
    bool hasToken() const { return token[0] != '\0'; }

    /// Czy urzadzenie ma komplet potrzebny do wyslania czegokolwiek.
    bool isComplete() const { return hasNetwork() && hasToken(); }

    void clear() { *this = IntegrationConfig{}; }
};

/// Ustawienie pola z odrzuceniem wartosci niepoprawnej.
///
/// Wartosc za dluga albo z niedozwolonymi znakami NIE jest zapisywana nawet
/// czesciowo — polowa hasla WiFi jest gorsza niz jego brak, bo wyglada jak
/// konfiguracja, a nigdy sie nie polaczy.
///
/// @return false gdy wartosc odrzucono; pole zostaje bez zmian
bool setSsid(IntegrationConfig& config, const char* value);
bool setPassword(IntegrationConfig& config, const char* value);
bool setToken(IntegrationConfig& config, const char* value);

/// Token do pokazania na ekranie: same cztery ostatnie znaki, reszta zakryta.
/// Pozwala sprawdzic "czy to ten token", nie pokazujac go osobie, ktora
/// akurat patrzy na motocykl.
void maskToken(const IntegrationConfig& config, char* out, size_t outSize);

}  // namespace telemetry
