// Motusy Moto Box — jedno miejsce, w ktorym zaokraglamy pomiary.
//
// DLACZEGO OSOBNY PLIK DLA JEDNEJ FUNKCJI: ta sama liczba pokazuje sie na
// ekranie urzadzenia i leci do API, a stamtad na strone. Jesli ekran mowi 25,
// a panel 26, uzytkownik ma prawo uznac, ze ktoras strona klamie — i nie ma
// jak rozstrzygnac ktora. Dopoki zaokraglenie istnialo w dwoch kopiach
// (ui/MainScreen i telemetry/TelemetryJson), rozjechanie ich bylo kwestia
// czasu i jednej nieuwaznej poprawki.
//
// POLOWKI W GORE, nie do parzystej. printf("%.0f") zaokragla 24,5 do 24,
// a 25,5 do 26 (bankierskie) — poprawne statystycznie, ale nie do wytlumaczenia
// komus, kto patrzy na dwie liczby na ekranie. Wartosci sa maksimami, wiec
// zawsze nieujemne; ujemnych ten wzor nie obsluguje i nie musi.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

namespace motion {

/// Zaokraglenie do liczby calkowitej, polowki w gore (25,5 -> 26).
/// Wartosci ujemne nie wystepuja — pomiary sa maksimami.
constexpr int roundHalfUp(float value) {
    return value <= 0.0f ? 0 : static_cast<int>(value + 0.5f);
}

/// Predkosc maksymalna w pelnych km/h, z podloga na jedynce.
///
/// Zero jest zarezerwowane dla "nie bylo czym zmierzyc" (brak GPS albo brak
/// fixu) i idzie do API jako null. Odbiornik, ktory zlapal pozycje, pokazuje
/// na postoju szum rzedu 0,3-0,5 km/h — to JEST pomiar, wiec nie moze
/// wygladac tak samo jak jego brak. Najnizszy wynik, jaki zobaczy uzytkownik,
/// to 1 km/h.
constexpr int roundSpeedKmh(float value) {
    return value <= 0.0f ? 0 : (roundHalfUp(value) > 0 ? roundHalfUp(value) : 1);
}

}  // namespace motion
