// Motusy Moto Box — komendy konfiguracji integracji wpisywane przez USB.
//
// Docelowym sposobem wpisania sieci i tokena jest formularz na telefonie (K4).
// Zanim on powstanie, potrzebna byla droga NAJKROTSZA: uzytkownik i tak siedzi
// przy komputerze, gdy kopiuje token ze strony, a monitor portu szeregowego
// jest juz otwarty przy kazdym wgrywaniu firmware.
//
// FORMAT `KLUCZ=wartosc`, nie jednoliterowe skroty jak w rejestratorze (L/D/X):
// haslo WiFi moze zawierac spacje, wiec potrzebny jest jednoznaczny separator.
// Wszystko po pierwszym znaku "=" jest wartoscia — dokladnie, bez obcinania
// odstepow. Obcinanie zamienialoby literowke w ciche przeklamanie hasla.
//
// Sama walidacja wartosci nalezy do IntegrationConfig — tutaj jest tylko
// rozbior linii. Czyste C++ bez zaleznosci od sprzetu, zeby dalo sie to
// sprawdzic testami na komputerze.

#pragma once

#include "IntegrationConfig.h"

namespace telemetry {

/// Co uzytkownik chcial zrobic.
enum class ConfigField {
    /// Linia nie jest komenda konfiguracji (moze byc komenda rejestratora).
    None,
    Ssid,
    Password,
    Token,
    /// Pokazanie biezacych ustawien.
    Status,
    /// Skasowanie calej konfiguracji.
    Clear,
    /// Sprawdzenie polaczenia i tokena tu i teraz. Przy biurku wysylka nie
    /// rusza sama — czeka na zgaszenie stacyjki — wiec bez tej komendy nie
    /// dalo by sie sprawdzic konfiguracji bez wyprowadzania motocykla.
    Verify,
};

struct ConfigCommandResult {
    ConfigField field = ConfigField::None;
    /// Czy wartosc zostala przyjeta. Falsz przy rozpoznanej komendzie oznacza
    /// wartosc odrzucona przez walidacje (za dluga, ze znakiem sterujacym,
    /// odstep w tokenie) — konfiguracja zostaje wtedy BEZ ZMIAN.
    bool accepted = false;
    /// Czy stan konfiguracji trzeba zapisac w pamieci nieulotnej.
    bool needsSave = false;
};

/// Wykonuje jedna linie komendy. Rozpoznawane klucze (wielkosc liter bez
/// znaczenia): `SIEC=` (alias `SSID=`), `HASLO=`, `TOKEN=`, oraz bezargumentowe
/// `STAN`, `TEST` i `KASUJ`.
///
/// @param line linia bez znaku konca wiersza
ConfigCommandResult applyConfigLine(IntegrationConfig& config, const char* line);

/// Nazwa pola do komunikatu na porcie szeregowym.
const char* configFieldName(ConfigField field);

}  // namespace telemetry
