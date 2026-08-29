// Motusy Moto Box — rekord przejazdu w postaci wysylanej na serwer.
//
// RideValues (lib/motion) opisuje POMIAR: piec liczb pokazywanych na ekranie.
// RideRecord dokłada to, czego potrzebuje serwer, zeby ulozyc te pomiary
// w historii konta: numer przejazdu, czas trwania i znacznik czasu.
//
// NUMER PRZEJAZDU (`seq`) jest sercem calej wysylki. Rosnie monotonicznie przez
// cale zycie urzadzenia i NIE jest zerowany przez reset wynikow. Para
// (device_id, seq) jest kluczem unikalnym po stronie API — dzieki temu powtorna
// wysylka tego samego przejazdu (bo potwierdzenie sie zgubilo) nie tworzy
// duplikatu, tylko trafia w istniejacy wpis.
//
// ZNACZNIK CZASU: urzadzenie nie ma RTC, wiec do czasu modulu GPS `recordedAt`
// zostaje zerem i idzie do API jako null. Data jest wartoscia informacyjna —
// kolejnosc przejazdow wynika z `seq`, nie z czasu.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstdint>

#include "RideMetrics.h"

namespace telemetry {

/// Tozsamosc urzadzenia dolaczana do kazdej przesylki.
struct DeviceIdentity {
    /// 12 znakow hex wyprowadzonych z fabrycznego MAC (eFuse). Identyfikator,
    /// nie sekret — uwierzytelnia token w naglowku, nie ten numer.
    const char* deviceId = "";
    /// Wersja firmware, do diagnostyki zgloszen typu "dziwne wyniki".
    const char* firmware = "";
    /// Czy urzadzenie ma kalibracje montazu. Bez niej pomiary nie sa zbierane,
    /// wiec serwer widzi od razu, czy pusty przejazd to wina uzytkownika.
    bool calibrated = false;
};

/// Jeden przejazd gotowy do wyslania.
struct RideRecord {
    /// Numer przejazdu w urzadzeniu; 0 oznacza rekord niezainicjowany.
    uint32_t seq = 0;
    motion::RideValues values{};
    /// Czas trwania przejazdu [s]. Liczony bez GPS.
    uint32_t durationS = 0;
    /// Unix timestamp konca przejazdu; 0 = nieznany (null w JSON).
    long long recordedAt = 0;
};

}  // namespace telemetry
