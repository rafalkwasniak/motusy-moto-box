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

#include "IntegrationConfig.h"
#include "MountCalibration.h"
#include "RideHistory.h"
#include "RideMetrics.h"

namespace hal {

/// Komplet danych trwalych urzadzenia.
struct PersistentState {
    motion::RideValues overall;
    motion::RideValues ride;

    /// Ile juz trwa biezacy przejazd [s] — zeby przezyl restart na baterii (§25).
    uint32_t rideDurationS = 0;

    /// §16 — stan modulu alarmowego. Domyslnie wlaczony: urzadzenie ma pilnowac
    /// motocykla, wiec brak zapisanego ustawienia nie powinien zostawiac go bez ochrony.
    bool alarmEnabled = true;

    /// Zapis sladu trasy. Domyslnie WYLACZONY — odwrotnie niz alarm i celowo:
    /// trasa to dane innej wagi niz kat przechylu i wymaga swiadomej zgody
    /// (docs/gpx-slad-trasy.md §1). Brak zapisanego ustawienia znaczy "nie".
    bool trackEnabled = false;

    bool mountCalibrated = false;
    motion::Mat3 mountRotation;

    /// Czy biezacy przejazd zostal juz przeniesiony do historii. Chroni przed
    /// zdublowaniem wpisu, gdy po zaniku zasilania urzadzenie jeszcze raz
    /// wstanie (np. po rozladowaniu baterii na parkingu).
    bool rideArchived = false;

    motion::RideHistory history;

    /// Numer nadany ostatniemu zarchiwizowanemu przejazdowi i numer ostatniego
    /// potwierdzonego przez serwer — caly stan kolejki wysylki (lib/telemetry).
    /// Dwie liczby zamiast flagi przy kazdym wpisie historii, dzieki czemu
    /// format historii i `kSchemaVersion` zostaja bez zmian.
    uint32_t lastRideSeq = 0;
    uint32_t sentThrough = 0;

    /// Siec domowa i token konta — wpisywane raz, przez ekran INTEGRACJA.
    telemetry::IntegrationConfig integration;
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

    /// Zapisuje oba zestawy wynikow wraz z flaga archiwizacji przejazdu
    /// i czasem trwania biezacego przejazdu.
    /// Wolane okresowo i przy zaniku zasilania.
    bool saveResults(const motion::RideValues& overall, const motion::RideValues& ride,
                     bool rideArchived, uint32_t rideDurationS);

    /// Zapisuje historie przejazdow wraz z czasami ich trwania.
    /// Wolane przy archiwizacji przejazdu.
    bool saveHistory(const motion::RideHistory& history);

    /// Stan kolejki wysylki. Zapisywany przy archiwizacji przejazdu i po
    /// potwierdzeniu z serwera — obie sytuacje sa rzadkie, wiec zapis idzie
    /// natychmiast.
    bool saveUploadState(uint32_t lastRideSeq, uint32_t sentThrough);

    /// Ustawienia integracji. Zapisywane po zatwierdzeniu formularza.
    bool saveIntegration(const telemetry::IntegrationConfig& integration);

    /// §22.1 — stan alarmu zmienia sie rzadko, wiec zapisujemy go natychmiast.
    bool saveAlarmEnabled(bool enabled);

    /// Zapis sladu trasy — tak samo rzadka zmiana jak alarm.
    bool saveTrackEnabled(bool enabled);

    /// §14 — kalibracja zmienia sie tylko na wyrazne zyczenie uzytkownika.
    bool saveMount(const motion::MountCalibration& mount);

    /// Czysci cala przestrzen nazw. Do diagnostyki i testow fabrycznych.
    bool clearAll();

    bool isAvailable() const { return available_; }

    /// Wersja schematu danych. Podniesienie uniewaznia zapisane dane
    /// zamiast czytac je w zlym formacie.
    /// v2 (2026-08-28): historia przejazdow + flaga archiwizacji.
    ///
    /// Stan kolejki wysylki (2026-08-29) CELOWO nie podniosl wersji: to dwa
    /// nowe klucze czytane z wartoscia domyslna, a nie zmiana ukladu istniejacych
    /// pol. Podbicie wersji skasowaloby uzytkownikowi kalibracje montazu.
    static constexpr uint32_t kSchemaVersion = 2;

private:
    Preferences prefs_;
    bool available_ = false;
};

}  // namespace hal
