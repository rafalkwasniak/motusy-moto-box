// Motusy Moto Box — pamiec nieulotna (§13 i §27 specyfikacji).
//
// Przechowuje: dwa zestawy wynikow, stan modulu alarmowego i kalibracje montazu.
// Dane musza przetrwac wylaczenie motocykla, restart i utrate zasilania.
//
// SERIALIZACJA JAWNA, nie zrzut struktury. Zapis `putBytes(&rideValues, sizeof)`
// wygladalby prosciej, ale wiazalby format danych na flashu z ukladem pol
// w pamieci — dodanie szostej wartosci w przyszlosci odczytaloby smieci.
// Dlatego pakujemy pole po polu, a `kSchemaVersion` pilnuje zgodnosci.
//
// STRATEGIA ZAPISU (architektura §6.2): wyniki zyja w RAM i sa zapisywane
// okresowo tylko gdy sie zmienily. Zapis przy kazdym nowym rekordzie zajechalby
// flash — w intensywnej jezdzie rekord poprawia sie kilkanascie razy na minute.

#pragma once

#include <Preferences.h>

#include "MountCalibration.h"
#include "RideHistory.h"
#include "RideMetrics.h"

namespace hal {

/// Komplet danych trwalych urzadzenia.
struct PersistentState {
    motion::RideValues overall;
    motion::RideValues ride;

    /// §16 — stan modulu alarmowego. Domyslnie wlaczony: urzadzenie ma pilnowac
    /// motocykla, wiec brak zapisanego ustawienia nie powinien zostawiac go bez ochrony.
    bool alarmEnabled = true;

    bool mountCalibrated = false;
    motion::Mat3 mountRotation;

    /// Czy biezacy przejazd zostal juz przeniesiony do historii. Chroni przed
    /// zdublowaniem wpisu, gdy po zaniku zasilania urzadzenie jeszcze raz
    /// wstanie (np. po rozladowaniu baterii na parkingu).
    bool rideArchived = false;

    motion::RideHistory history;
};

enum class LoadResult {
    /// Odczytano zapisane dane.
    Restored,
    /// Pamiec pusta — pierwsze uruchomienie urzadzenia.
    Fresh,
    /// Zapisany schemat pochodzi z innej wersji firmware; dane odrzucone.
    Migrated,
    /// Blad dostepu do NVS.
    Failed,
};

class Store {
public:
    /// @return false gdy NVS jest niedostepny — urzadzenie dziala dalej,
    ///         ale wyniki nie przetrwaja restartu.
    bool begin();

    LoadResult load(PersistentState& out);

    /// Zapisuje oba zestawy wynikow wraz z flaga archiwizacji przejazdu.
    /// Wolane okresowo i przy zaniku zasilania.
    bool saveResults(const motion::RideValues& overall, const motion::RideValues& ride,
                     bool rideArchived);

    /// Zapisuje historie przejazdow. Wolane przy archiwizacji przejazdu.
    bool saveHistory(const motion::RideHistory& history);

    /// §22.1 — stan alarmu zmienia sie rzadko, wiec zapisujemy go natychmiast.
    bool saveAlarmEnabled(bool enabled);

    /// §14 — kalibracja zmienia sie tylko na wyrazne zyczenie uzytkownika.
    bool saveMount(const motion::MountCalibration& mount);

    /// Czysci cala przestrzen nazw. Do diagnostyki i testow fabrycznych.
    bool clearAll();

    bool isAvailable() const { return available_; }

    /// Wersja schematu danych. Podniesienie uniewaznia zapisane dane
    /// zamiast czytac je w zlym formacie.
    /// v2 (2026-08-28): historia przejazdow + flaga archiwizacji.
    static constexpr uint32_t kSchemaVersion = 2;

private:
    Preferences prefs_;
    bool available_ = false;
};

}  // namespace hal
