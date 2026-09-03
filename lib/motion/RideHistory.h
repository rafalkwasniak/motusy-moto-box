// Motusy Moto Box — historia ostatnich przejazdow.
//
// Motywacja: po wylaczeniu stacyjki ekran gasnie po 2 minutach — kto nie
// zdazyl przeczytac wynikow, ten je stracil. Historia pozwala obejrzec caly
// dzien jazdy wieczorem: 10 ostatnich przejazdow, od najnowszego.
//
// Bufor pierscieniowy; indeks 0 to zawsze najnowszy przejazd. Puste przejazdy
// (urzadzenie wlaczone, motocykl nieruszony) nie sa zapisywane — nie ma sensu
// zapelniac historii zerami z samego krecenia kluczykiem.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

#include "RideMetrics.h"

namespace motion {

class RideHistory {
public:
    static constexpr size_t kCapacity = 10;

    /// Doklada przejazd jako najnowszy. Pusty przejazd jest ignorowany.
    ///
    /// Czas trwania jedzie OBOK wynikow, a nie w `RideValues`, bo `RideValues`
    /// opisuje pomiar pokazywany na ekranie i jego uklad w pamieci nieulotnej
    /// jest przypiety do wersji schematu — dolozenie tam szostego pola
    /// skasowaloby uzytkownikowi kalibracje.
    ///
    /// Znacznik czasu jedzie obok z tego samego powodu, co czas trwania:
    /// `RideValues` opisuje pomiar i jego uklad w NVS jest przypiety do wersji
    /// schematu.
    ///
    /// @return true jesli przejazd zostal zapisany
    bool push(const RideValues& ride, uint32_t durationS = 0, uint32_t recordedAt = 0);

    size_t count() const { return count_; }

    /// Przejazd o zadanym indeksie: 0 = najnowszy. Indeks poza zakresem
    /// zwraca pusty zestaw.
    const RideValues& at(size_t index) const;

    /// Czas trwania przejazdu o zadanym indeksie [s]. Zero poza zakresem
    /// oraz dla wpisow sprzed wprowadzenia pomiaru czasu.
    uint32_t durationAt(size_t index) const;

    /// Uniksowy znacznik konca przejazdu. Zero = nieznany (przejazd bez fixa
    /// GPS albo wpis sprzed modulu) i idzie do API jako null.
    uint32_t recordedAtAt(size_t index) const;

    void clear();

    /// Odtworzenie z pamieci nieulotnej: `rides[0]` to najnowszy przejazd.
    /// `durations` i `recordedAt` moga byc nullptr — wtedy odpowiednie
    /// wartosci sa zerowe (wpisy sprzed wprowadzenia tych pomiarow).
    void restore(const RideValues* rides, const uint32_t* durations, size_t count,
                 const uint32_t* recordedAt = nullptr);

private:
    RideValues slots_[kCapacity] = {};
    uint32_t durations_[kCapacity] = {};
    uint32_t recordedAt_[kCapacity] = {};
    /// Indeks najnowszego wpisu w buforze pierscieniowym.
    size_t head_ = 0;
    size_t count_ = 0;

    static const RideValues kEmpty;
};

/// Czy zestaw nie zawiera zadnego pomiaru.
bool isEmptyRide(const RideValues& ride);

}  // namespace motion
