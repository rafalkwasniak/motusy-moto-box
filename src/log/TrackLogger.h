// Motusy Moto Box — zapis sladu trasy na flashu (docs/gpx-slad-trasy.md §4).
//
// SLAD IDZIE NA FLASH W TRAKCIE JAZDY, NIE DO RAM. Zanik zasilania w piatej
// godzinie nie moze kosztowac calej trasy, a szesciogodzinny przejazd to
// ~50 kB — wiecej, niz warto trzymac w pamieci obok stosu TLS.
//
// PLIK JEST PRZESYLKA. To, co lezy na flashu, idzie potem w ciele zadania POST
// bajt w bajt (docs/api-slad-trasy.md). Dlatego format jest tekstowy i
// doklejalny, a nie binarny jak w RawLogger: `HTTPClient` POST-uje ze `Stream`
// prosto z pliku, wiec nic nie trzeba przepakowywac ani skladac w RAM.
//
// NUMER PRZEJAZDU NADAJE SIE PO JEZDZIE, a plik powstaje w jej trakcie —
// stad plik roboczy o stalej nazwie, przemianowywany dopiero przy archiwizacji.
//
// PLIK ROBOCZY ZASTANY PRZY STARCIE NIE JEST SMIECIEM. Restart w trakcie jazdy
// (zanik zasilania, watchdog, rozladowana bateria) zostawia slad bez numeru,
// ale przejazd, do ktorego nalezy, przezywa restart w NVS — wiec slad da sie
// dokonczyc. Kasowanie takiego pliku byloby najprostsze i najgorsze: w piatej
// godzinie trasy kosztowaloby cala trase, czyli dokladnie to, przed czym mial
// chronic zapis na flash zamiast do RAM. `resumeRide()` odtwarza stan i pisze
// dalej; `discardWorkFile()` jest dla prawdziwych sierot.
//
// Ta sama partycja i ten sam system plikow co RawLogger. Oba moduly wolaja
// LittleFS.begin() — drugie wywolanie na zamontowanym systemie nic nie kosztuje.

#pragma once

#include <Arduino.h>
#include <FS.h>

#include "TrackFormat.h"

namespace tracklog {

/// Wynik wznowienia zapisu po restarcie.
struct TrackResume {
    bool ok = false;
    /// Ostatni punkt zapisany przed restartem — od niego licza sie dalsze delty.
    track::Point last{};
    /// Czy slad niesie prawdziwy czas UTC. Tryb MUSI zostac ten sam, bo
    /// serwer traktuje `t0=0` jako "caly slad bez czasu".
    bool timed = false;
    uint32_t points = 0;
};

class TrackLogger {
public:
    /// Ile domknietych sladow trzymamy. Tyle samo, ile przejazdow trzyma
    /// historia — slad przejazdu, ktorego juz nie ma, nie ma do czego wrocic.
    static constexpr size_t kMaxTracks = 10;

    /// Ile miejsca chcemy miec wolnego przed rozpoczeciem zapisu. Szesc godzin
    /// jazdy to ~50 kB; 128 kB daje zapas na gesta zabudowe i na to, ze
    /// LittleFS liczy zajetosc blokami.
    static constexpr size_t kReserveBytes = 128 * 1024;

    /// Co ile domykamy zapis na flashu. Nie czesciej, bo kazdy flush to cykl
    /// kasowania bloku; nie rzadziej, bo tyle wlasnie traci sie przy zaniku
    /// zasilania — przy jednym punkcie na kilka sekund to jeden, moze dwa punkty.
    static constexpr uint32_t kFlushIntervalMs = 5000;

    /// Montuje system plikow na partycji "storage". NIE rusza pliku roboczego —
    /// o jego losie decyduje wolajacy, bo tylko on wie, czy przejazd trwa.
    /// @return false gdy partycja niedostepna — logger pozostaje niemy.
    bool begin();
    bool isMounted() const { return mounted_; }

    /// Czy na flashu lezy niedomkniety slad.
    bool hasWorkFile() const;

    /// Kasuje plik roboczy. Wolac wylacznie dla sieroty — czyli sladu
    /// przejazdu, ktory zostal juz zarchiwizowany albo nigdy nie wroci.
    void discardWorkFile();

    /// Wznawia zapis do zastanego pliku roboczego po restarcie w trakcie jazdy.
    /// Plik jest przy okazji przycinany do ostatniej CALEJ linii — przerwany
    /// zapis moze zostawic ogon, ktory uniewaznilby cala przesylke.
    /// @return `ok == false` gdy pliku nie ma albo nie da sie go odczytac.
    TrackResume resumeRide(const track::TrackHeader& header);

    /// Nowy przejazd. Plik powstaje dopiero przy PIERWSZYM punkcie: przejazd
    /// bez zasiegu satelitow nie ma zostawiac pustego pliku na flashu.
    void startRide(const track::TrackHeader& header);

    /// Dopisuje punkt sladu.
    void write(const track::Point& point, uint32_t nowMs);

    /// Okresowe domkniecie zapisu. Wolac z petli glownej.
    void tick(uint32_t nowMs);

    /// Domyka slad i nadaje mu numer przejazdu.
    /// @return false gdy nie bylo czego domykac (przejazd bez ani jednego punktu).
    bool finishRide(uint32_t seq);

    /// Porzuca biezacy zapis i kasuje plik roboczy. Uzywane, gdy uzytkownik
    /// wylaczy slad w trakcie jazdy.
    void abortRide();

    bool isRecording() const { return recording_; }
    uint32_t pointsWritten() const { return points_; }
    uint32_t bytesWritten() const { return bytes_; }

    /// Komendy diagnostyczne po USB:
    ///     SLADY       lista sladow i wolne miejsce
    ///     SLADY <nr>  zrzut sladu o podanym numerze przejazdu
    ///     SLADY X     skasowanie wszystkich sladow
    /// @return false gdy linia nie jest komenda sladow.
    bool handleCommand(Stream& io, const char* command);

private:
    void openFile();
    /// Czysci stan zapisu, NIE ruszajac pliku na flashu.
    void abortRideState();
    void flushBuffer();
    /// Usuwa najstarsze slady, az bedzie `kReserveBytes` wolnego i najwyzej
    /// `kMaxTracks` plikow.
    void makeRoom();

    bool mounted_ = false;
    bool recording_ = false;
    /// Plik jest otwarty. Falsz przy `recording_` znaczy "czekamy na pierwszy fix".
    bool open_ = false;

    File file_;
    track::TrackWriter writer_;
    track::TrackHeader header_{};
    /// Naglowek wskazuje na napisy z zewnatrz, wiec trzymamy wlasne kopie —
    /// wersja firmware jest stala, ale device_id juz nie musi byc.
    char deviceId_[16] = {};
    char firmware_[16] = {};

    uint8_t buffer_[256] = {};
    size_t fill_ = 0;
    uint32_t lastFlushMs_ = 0;

    uint32_t points_ = 0;
    uint32_t bytes_ = 0;
};

}  // namespace tracklog
