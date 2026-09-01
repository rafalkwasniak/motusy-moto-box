#include "ConfigCommand.h"

#include <cstring>

namespace telemetry {
namespace {

bool isBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

/// Porownanie klucza z wzorcem: bez rozroznienia wielkosci liter, z pominieciem
/// odstepow na obu koncach. `keyEnd` wskazuje za ostatni znak klucza.
bool keyEquals(const char* key, const char* keyEnd, const char* pattern) {
    while (key < keyEnd && isBlank(*key)) ++key;
    while (keyEnd > key && isBlank(*(keyEnd - 1))) --keyEnd;

    for (; key < keyEnd; ++key, ++pattern) {
        if (*pattern == '\0') return false;
        if (upper(*key) != *pattern) return false;
    }
    return *pattern == '\0';
}

}  // namespace

ConfigCommandResult applyConfigLine(IntegrationConfig& config, const char* line) {
    ConfigCommandResult result;
    if (line == nullptr) return result;

    const char* separator = std::strchr(line, '=');

    // Komendy bezargumentowe.
    if (separator == nullptr) {
        const char* end = line + std::strlen(line);
        if (keyEquals(line, end, "STAN")) {
            result.field = ConfigField::Status;
            result.accepted = true;
        } else if (keyEquals(line, end, "TEST")) {
            result.field = ConfigField::Verify;
            result.accepted = true;
        } else if (keyEquals(line, end, "KASUJ")) {
            config.clear();
            result.field = ConfigField::Clear;
            result.accepted = true;
            result.needsSave = true;
        }
        return result;
    }

    // Wartosc bierzemy DOSLOWNIE, razem z ewentualnymi odstepami: "HASLO= tajne"
    // ma ustawic haslo zaczynajace sie spacja, bo tak wlasnie brzmi wpisane
    // haslo. Ciche obcinanie dawaloby konfiguracje, ktora wyglada dobrze,
    // a nigdy sie nie polaczy.
    const char* value = separator + 1;

    if (keyEquals(line, separator, "SIEC") || keyEquals(line, separator, "SSID")) {
        result.field = ConfigField::Ssid;
        result.accepted = setSsid(config, value);
    } else if (keyEquals(line, separator, "HASLO")) {
        result.field = ConfigField::Password;
        result.accepted = setPassword(config, value);
    } else if (keyEquals(line, separator, "TOKEN")) {
        result.field = ConfigField::Token;
        result.accepted = setToken(config, value);
    }

    result.needsSave = result.accepted;
    return result;
}

const char* configFieldName(ConfigField field) {
    switch (field) {
        case ConfigField::Ssid: return "siec";
        case ConfigField::Password: return "haslo";
        case ConfigField::Token: return "token";
        case ConfigField::Status: return "stan";
        case ConfigField::Verify: return "test";
        case ConfigField::Clear: return "konfiguracja";
        case ConfigField::None: break;
    }
    return "";
}

}  // namespace telemetry
