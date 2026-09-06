// Motusy Moto Box — wynik pomiaru halasu dla JEDNEGO przejazdu.
//
// GDZIE TO MIESZKA I DLACZEGO NIE W RideValues. `motion::RideValues` opisuje
// piec liczb POKAZYWANYCH NA EKRANIE, a jego uklad w pamieci nieulotnej jest
// przypiety do `hal::Store::kSchemaVersion`. Dolozenie tam szostego pola
// skasowaloby uzytkownikowi kalibracje montazu, a bez niej pomiary celowo nie
// sa zbierane.
//
// Halas na ekran nie idzie (decyzja uzytkownika — wylacznie przez API), wiec
// jedzie OBOK, dokladnie tak jak czas trwania i znacznik czasu przejazdu.
// Ten sam wzorzec, ta sama motywacja, zero migracji.
//
// DLACZEGO LICZBY CALKOWITE. Poziom w 0,1 dB, predkosc w pelnych km/h:
// format jest wtedy jawny i niezalezny od ukladu float w pamieci, a rozmiar
// wpisu staly (9 bajtow). Rozdzielczosc 0,1 dB i tak jest wyzsza niz koszyk
// histogramu (0,5 dB), wiec nic nie tracimy.
//
// DLACZEGO LICZNIKI JADA RAZEM Z WYNIKIEM. Wartosc nie trafia na ekran, wiec
// nie ma jak zobaczyc, ze pomiar padl — a martwy mikrofon nie zglasza bledu,
// tylko oddaje podloge szumu, ktora wyglada jak cicha jazda. Bez licznikow
// przesterowan i zgubionych probek awaria jest NIEODROZNIALNA od ciszy.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

namespace noise {

struct RideNoise {
    /// Brak pomiaru. NIE zero: cichy przejazd i przejazd bez mikrofonu to dwie
    /// rozne rzeczy, a do API brak idzie jako null (tak jak `speed_kmh`).
    static constexpr int16_t kNoValue = -32768;

    /// Rozmiar wpisu w pamieci nieulotnej. Staly, wiec pozycje w tablicy
    /// wylicza sie arytmetyka, a nie parsowaniem.
    static constexpr size_t kPackedBytes = 9;

    /// Najwyzszy utrzymany poziom przejazdu w 0,1 dB(A).
    int16_t maxDb10 = kNoValue;

    /// Predkosc w chwili ustanowienia rekordu [km/h]. Zero = nieznana
    /// (przejazd bez fixa GPS). Sluzy do odroznienia wydechu od wiatru:
    /// jesli ta liczba prawie zawsze rowna sie predkosci maksymalnej
    /// przejazdu, mierzymy powietrze — patrz docs/pomiar-halasu.md §6.
    uint16_t atSpeedKmh = 0;

    /// Ile probek dobilo do pelnej skali. Niezerowe znaczy, ze wynik jest
    /// ">= X", nigdy "X".
    uint16_t clipped = 0;

    /// Ile probek przepadlo na przerwach w strumieniu I2S.
    uint16_t dropped = 0;

    /// Znacznik serii pomiarowej (cfg::kNoiseCalibrationVersion). Po nim
    /// serwer odroznia dwie serie od jednej, gdy zmieni sie wzmocnienie
    /// albo montaz.
    uint8_t calibration = 0;

    bool measured() const { return maxDb10 != kNoValue; }

    /// Podnosi wynik do wyzszego z dwoch, razem z predkoscia, przy ktorej
    /// padl. Liczniki biora maksimum, bo sa narastajace.
    ///
    /// PO CO: restart na baterii w trakcie jazdy zaczyna pomiar od zera, ale
    /// przejazd trwa dalej i jego rekord ma przezyc. Ta sama zasada, co przy
    /// czasie trwania i sladzie trasy — restart nie kasuje jazdy.
    void raiseTo(const RideNoise& other);
};

/// Sklada wynik z liczb zmiennoprzecinkowych toru pomiarowego.
///
/// Zaokraglanie jest TUTAJ i tylko tutaj. Reguly z motion::Rounding celowo nie
/// uzywamy — tamta istnieje, zeby ekran i JSON pokazywaly te sama liczbe,
/// a halas na ekran nie idzie, wiec miejsce zaokraglenia jest jedno.
///
/// @param maxDb      poziom w dB(A); NoiseMeter::kNoData daje brak pomiaru
/// @param speedKmh   predkosc w chwili rekordu
/// @param clipped    licznik przesterowan (nasycany do 65535)
/// @param dropped    licznik zgubionych probek (nasycany do 65535)
RideNoise makeRideNoise(float maxDb, float speedKmh, uint32_t clipped, uint32_t dropped,
                        uint8_t calibration);

/// Zapis do bufora o rozmiarze kPackedBytes. Jawny, bajt po bajcie —
/// zrzut struktury wiazalby format na flashu z ukladem pol w pamieci.
void packRideNoise(const RideNoise& value, uint8_t* out);

void unpackRideNoise(const uint8_t* in, RideNoise& out);

}  // namespace noise
