// Motusy Moto Box — rejestrator surowych danych IMU (etap E3, czesc zapisu).
//
// Cel: nagranie prawdziwej jazdy motocyklem jako materialu do strojenia
// algorytmow (architektura §10.3). Bez tych danych progi filtru orientacji
// i detekcji ruchu stroi sie na sciepo.
//
// FORMAT. Zapis binarny, nie CSV — tekst zajalby ~2,5x wiecej flasha.
// Plik: naglowek 8 B ("MMB1" + wersja u32), potem rekordy po 28 B:
//     uint32  t_ms                 czas od startu urzadzenia
//     float   ax, ay, az           przyspieszenie [g], uklad urzadzenia
//     float   gx, gy, gz           predkosc katowa [rad/s], uklad urzadzenia
// Wszystko little-endian. Konwersja do CSV nastepuje dopiero przy zgrywaniu
// przez USB — patrz handleCommand().
//
// CYKL ZYCIA. Sesja zapisu = sesja jazdy: startuje z LOTKA, konczy sie
// z potwierdzonym zanikiem zasilania. Kazda sesja to osobny plik /log_NNN.bin.
// Przy braku miejsca najstarsze pliki sa usuwane; w trakcie sesji zapis
// zatrzymuje sie przy zapelnieniu (pierwsze ~28 min jazdy zostaje).
//
// PROBKOWANIE. Dekymacja czasowa do ~100 Hz (IMU potrafi oddawac ~165 Hz):
// 2,8 kB/s -> 4,75 MB partycji starcza na ~28 minut. Znaczniki czasu w
// rekordach czynia format samoopisujacym sie co do tempa.
//
// ZGRYWANIE (przy biurku, po USB):
//     L        lista plikow + wolne miejsce
//     D<nr>    zrzut pliku nr jako CSV na port szeregowy
//     X        skasowanie wszystkich logow
//
// Calosc kompilowana tylko przy MMB_RAW_LOGGER=1 (platformio.ini).

#pragma once

#include <FS.h>

#include "Vec3.h"

namespace rawlog {

class RawLogger {
public:
    /// Montuje system plikow na partycji "storage" (formatuje przy pierwszym
    /// uzyciu). @return false gdy partycja niedostepna — logger pozostaje niemy.
    bool begin();

    /// Otwiera nowy plik sesji. Usuwa najstarsze logi, jesli brak miejsca.
    void startSession(uint32_t nowMs);

    /// Domyka plik sesji (bufor + flush). Bezpieczne przy braku sesji.
    void stopSession();

    /// Dopisuje probke. Ignorowane poza sesja. Dekymacja do ~100 Hz w srodku.
    void log(const motion::ImuSample& sample);

    /// Wykonuje komende zgrywania (L, D<nr>, X). Linie z portu szeregowego
    /// czyta main.cpp i rozdziela miedzy rejestrator a konfiguracje integracji —
    /// dwa niezalezne czytniki tego samego portu podkradalyby sobie znaki.
    ///
    /// @return false gdy linia nie jest komenda rejestratora
    bool handleCommand(Stream& io, const char* command);

    bool isMounted() const { return mounted_; }
    bool isLogging() const { return active_; }
    /// Zapis przerwany z powodu pelnej partycji.
    bool isFull() const { return full_; }

private:
    void writeRecord(const motion::ImuSample& sample);
    void flushBuffer();
    void listFiles(Stream& io);
    void dumpFile(Stream& io, unsigned number);
    void deleteAll(Stream& io);
    unsigned nextFileNumber();
    void freeSpaceFor(size_t requiredBytes);

    static constexpr size_t kRecordBytes = 4 + 6 * 4;
    static constexpr size_t kBufferBytes = 512;
    /// Minimalna wolna przestrzen, ponizej ktorej sesja jest zatrzymywana.
    static constexpr size_t kStopMarginBytes = 64 * 1024;
    /// Tyle miejsca probujemy zwolnic przed nowa sesja.
    static constexpr size_t kSessionReserveBytes = 600 * 1024;
    /// Minimalny odstep miedzy zapisanymi probkami [ms] — dekymacja do ~100 Hz.
    static constexpr uint32_t kMinSampleGapMs = 10;
    /// Co ile domykac dane na flashu, zeby przezyly nagly zanik zasilania.
    static constexpr uint32_t kFlushIntervalMs = 2000;

    File file_;
    bool mounted_ = false;
    bool active_ = false;
    bool full_ = false;

    uint8_t buffer_[kBufferBytes];
    size_t fill_ = 0;
    uint32_t lastSampleMs_ = 0;
    uint32_t lastFlushMs_ = 0;
};

}  // namespace rawlog
