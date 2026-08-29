// Motusy Moto Box — zamiana przejazdow na JSON i odczyt odpowiedzi serwera.
//
// Dlaczego wlasny generator, a nie biblioteka JSON: przesylka ma staly,
// znany ksztalt (naglowek + tablica najwyzej dziesieciu przejazdow), miesci sie
// w kilku kilobajtach i powstaje w buforze podanym przez wolajacego — bez sterty.
// Pelna biblioteka kosztowalaby pamiec i zaleznosc, a nie dala nic ponad to.
//
// WSZYSTKO PRZEZ BUFOR WOLAJACEGO: funkcja nigdy nie alokuje. Za maly bufor to
// nie jest polowiczny JSON, tylko zero — lepiej nie wyslac nic niz wyslac
// obciety rekord, ktory serwer zapisze jako prawidlowy.
//
// ODPOWIEDZ SERWERA jest jedna liczba: numer ostatniego przyjetego przejazdu.
// Wybor swiadomy — urzadzenie wysyla przejazdy w kolejnosci i pamieta jeden
// znacznik "wyslane do numeru N", wiec lista przyjetych identyfikatorow nie
// wnioslaby nic, a wymagalaby prawdziwego parsera JSON na urzadzeniu.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

#include "RideRecord.h"

namespace telemetry {

/// Najwiecej przejazdow w jednej przesylce — tyle, ile miesci historia.
constexpr size_t kMaxRidesPerPayload = 10;

/// Bufor wystarczajacy na pelna przesylke z zapasem.
constexpr size_t kMaxPayloadBytes = 4096;

/// Sklada tresc zadania POST.
///
/// @param out      bufor wolajacego; przy powodzeniu konczy sie zerem
/// @return dlugosc tekstu bez konczacego zera, albo 0 gdy dane sa niepoprawne
///         (za duzo przejazdow, niedozwolone znaki w identyfikatorach)
///         albo bufor jest za maly. Przy zerze zawartosc `out` jest bez wartosci.
size_t buildPayload(const DeviceIdentity& device, const RideRecord* rides, size_t count,
                    char* out, size_t outSize);

/// Wyciaga z odpowiedzi serwera numer ostatniego przyjetego przejazdu.
///
/// Szuka pola `accepted_through`. Celowo nie jest to pelny parser JSON —
/// interesuje nas jedna liczba w odpowiedzi o znanym ksztalcie.
///
/// @return false gdy pola nie ma albo nie zawiera liczby; `seqOut` bez zmian
bool parseAcceptedThrough(const char* json, uint32_t& seqOut);

}  // namespace telemetry
