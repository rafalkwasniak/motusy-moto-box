// Motusy Moto Box — fabryczny identyfikator urzadzenia.
//
// Dwanascie znakow hex wyprowadzonych z adresu MAC zapisanego w eFuse. Jest
// staly przez cale zycie ukladu, nie da sie go zmienic i nie zalezy od stanu
// pamieci — dlatego to on, a nie cokolwiek zapisanego w NVS, jest kluczem
// przejazdow po stronie API.
//
// IDENTYFIKATOR, NIE SEKRET. Adres MAC widac w eterze przy kazdym polaczeniu
// z siecia. Uwierzytelnia token w naglowku, nie ten numer.

#pragma once

namespace hal {

/// Zwraca wskaznik na bufor statyczny "a1b2c3d4e5f6" (male litery, zawsze
/// dwanascie znakow). Pierwsze wywolanie odczytuje eFuse, kolejne oddaja
/// gotowa wartosc.
const char* deviceId();

}  // namespace hal
