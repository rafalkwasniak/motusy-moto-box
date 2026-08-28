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

#include "RideMetrics.h"

namespace motion {

class RideHistory {
public:
    static constexpr size_t kCapacity = 10;

    /// Doklada przejazd jako najnowszy. Pusty przejazd jest ignorowany.
    /// @return true jesli przejazd zostal zapisany
    bool push(const RideValues& ride);

    size_t count() const { return count_; }

    /// Przejazd o zadanym indeksie: 0 = najnowszy. Indeks poza zakresem
    /// zwraca pusty zestaw.
    const RideValues& at(size_t index) const;

    void clear();

    /// Odtworzenie z pamieci nieulotnej: `rides[0]` to najnowszy przejazd.
    void restore(const RideValues* rides, size_t count);

private:
    RideValues slots_[kCapacity] = {};
    /// Indeks najnowszego wpisu w buforze pierscieniowym.
    size_t head_ = 0;
    size_t count_ = 0;

    static const RideValues kEmpty;
};

/// Czy zestaw nie zawiera zadnego pomiaru.
bool isEmptyRide(const RideValues& ride);

}  // namespace motion
