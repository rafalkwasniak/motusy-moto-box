// Motusy Moto Box — kolejka przejazdow czekajacych na wyslanie.
//
// CALY STAN KOLEJKI TO DWIE LICZBY:
//
//     lastSeq      — numer nadany ostatniemu zarchiwizowanemu przejazdowi
//     sentThrough  — numer ostatniego przejazdu potwierdzonego przez serwer
//
// Nie ma flagi "wyslane" przy kazdym wpisie historii i to jest decyzja
// projektowa, nie oszczednosc. Historia jest uporzadkowana od najnowszego, wiec
// numer wpisu wynika z jego pozycji: wpis nr 0 ma numer `lastSeq`, nr 1 ma
// `lastSeq - 1` i tak dalej. Dzieki temu format historii w NVS zostaje bez
// zmian, a `Store::kSchemaVersion` nie musi rosnac — a podniesienie wersji
// skasowaloby uzytkownikowi kalibracje montazu.
//
// NUMERY NIGDY SIE NIE POWTARZAJA I NIE COFAJA. Reset wynikow (§15) ich nie
// rusza: dla serwera liczy sie para (device_id, seq) i powtorzony numer
// nadpisalby cudzy przejazd.
//
// PRZEPELNIENIE HISTORII: historia trzyma dziesiec przejazdow. Jesli uzbiera
// sie wiecej niewyslanych, najstarsze przepadaja razem z historia — kolejka
// nie udaje, ze je ma. Zdarzy sie to dopiero po dziesieciu przejazdach z rzedu
// bez zasiegu.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>
#include <cstdint>

#include "RideHistory.h"
#include "RideRecord.h"

namespace telemetry {

class UploadQueue {
public:
    /// Odtworzenie z pamieci nieulotnej. Znacznik wyslania jest przycinany do
    /// numeru ostatniego przejazdu — dane z przyszlosci oznaczalyby uszkodzony
    /// zapis i zablokowalyby wysylke na zawsze.
    void restore(uint32_t lastSeq, uint32_t sentThrough);

    /// Przejazd trafil wlasnie do historii. @return nadany numer.
    uint32_t onRideArchived();

    uint32_t lastSeq() const { return lastSeq_; }
    uint32_t sentThrough() const { return sentThrough_; }

    /// Ile przejazdow czeka na wyslanie. Ograniczone tym, co realnie zostalo
    /// w historii.
    size_t pendingCount(size_t historyCount) const;

    /// Numer przejazdu na danej pozycji historii (0 = najnowszy).
    /// @return 0 gdy pozycja jest poza historia
    uint32_t seqAt(size_t historyIndex, size_t historyCount) const;

    /// Kompletuje przesylke: NAJSTARSZE zalegle przejazdy najpierw, numery
    /// rosnaco i bez przerw. Ciaglosc jest wymogiem — serwer potwierdza jedna
    /// liczba "przyjeto do numeru N", wiec dziura w srodku byłaby nie do
    /// odroznienia od odrzucenia.
    ///
    /// @return liczba wypelnionych rekordow
    size_t collect(const motion::RideHistory& history, RideRecord* out, size_t maxOut) const;

    /// Potwierdzenie z serwera. Numer starszy niz znacznik albo nowszy niz
    /// ostatni przejazd jest ignorowany.
    ///
    /// @return true gdy znacznik sie przesunal — tylko wtedy warto zapisac NVS
    bool confirmSentThrough(uint32_t seq);

private:
    uint32_t lastSeq_ = 0;
    uint32_t sentThrough_ = 0;
};

}  // namespace telemetry
