#include "TrackLogger.h"

#include <LittleFS.h>

#include <cstdio>
#include <cstring>

namespace tracklog {
namespace {

constexpr const char* kPartitionLabel = "storage";

/// Plik biezacego przejazdu. Stala nazwa, bo numeru jeszcze nie znamy.
constexpr const char* kWorkFile = "/trk_cur.txt";

/// Plik pomocniczy przy przycinaniu sladu po przerwanym zapisie. Nazwa celowo
/// nie pasuje do wzorca `trk_<numer>.txt`, wiec nie liczy sie jako slad.
constexpr const char* kTempFile = "/trk_tmp.txt";

/// Nazwa domknietego sladu: /trk_51.txt
void trackName(char* out, size_t size, uint32_t seq) {
    std::snprintf(out, size, "/trk_%lu.txt", static_cast<unsigned long>(seq));
}

bool parseSeq(const char* name, uint32_t& out) {
    unsigned long value = 0;
    if (std::sscanf(name, "trk_%lu.txt", &value) == 1 ||
        std::sscanf(name, "/trk_%lu.txt", &value) == 1) {
        out = static_cast<uint32_t>(value);
        return true;
    }
    return false;
}

/// Ile domknietych sladow lezy na flashu i jaki jest najnizszy numer.
size_t countTracks(uint32_t& lowestSeq) {
    size_t count = 0;
    lowestSeq = 0;
    File root = LittleFS.open("/");
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        uint32_t seq = 0;
        if (!parseSeq(entry.name(), seq)) continue;
        if (count == 0 || seq < lowestSeq) lowestSeq = seq;
        ++count;
    }
    return count;
}

}  // namespace

bool TrackLogger::begin() {
    // formatOnFail: pierwsze uruchomienie po zmianie tabeli partycji zastaje
    // smieci po dawnym slocie aplikacji.
    mounted_ = LittleFS.begin(true, "/littlefs", 10, kPartitionLabel);
    if (!mounted_) return false;

    // Niedokonczone przyciecie z poprzedniego startu. Plik roboczy jest wtedy
    // nietkniety, wiec zostaje tylko posprzatac po sobie.
    if (LittleFS.exists(kTempFile)) LittleFS.remove(kTempFile);

    // Pliku roboczego NIE ruszamy: o tym, czy to sierota, czy przerwany
    // przejazd, wie wylacznie wolajacy.
    return true;
}

bool TrackLogger::hasWorkFile() const {
    return mounted_ && LittleFS.exists(kWorkFile);
}

void TrackLogger::discardWorkFile() {
    if (mounted_ && LittleFS.exists(kWorkFile)) LittleFS.remove(kWorkFile);
}

TrackResume TrackLogger::resumeRide(const track::TrackHeader& header) {
    TrackResume result;
    if (!mounted_ || !LittleFS.exists(kWorkFile)) return result;

    abortRideState();

    File source = LittleFS.open(kWorkFile, FILE_READ);
    if (!source) return result;

    File target = LittleFS.open(kTempFile, FILE_WRITE);
    if (!target) {
        source.close();
        return result;
    }

    // Przepisujemy wylacznie linie, ktore skaner przyjal. Przy zapisie
    // sekwencyjnym uszkodzona moze byc tylko ostatnia — a ogon, ktorego serwer
    // nie zparsuje, uniewaznilby CALA przesylke, nie samego siebie.
    track::TrackScanner scanner;
    scanner.reset();

    char line[160];
    size_t length = 0;
    bool truncated = false;

    while (source.available() && !truncated) {
        const int c = source.read();
        if (c < 0) break;

        if (c == '\n') {
            line[length] = '\0';
            if (!scanner.feedLine(line)) {
                truncated = true;
                break;
            }
            target.write(reinterpret_cast<const uint8_t*>(line), length);
            target.write(static_cast<uint8_t>('\n'));
            length = 0;
            continue;
        }

        if (c == '\r') continue;  // tolerancja na CRLF

        if (length + 1 >= sizeof(line)) {
            truncated = true;  // linia dluzsza niz format dopuszcza
            break;
        }
        line[length++] = static_cast<char>(c);
    }
    // Ogon bez znaku konca linii to przerwany zapis — po prostu go nie ma.

    const uint32_t kept = static_cast<uint32_t>(target.size());
    source.close();
    target.close();

    if (!scanner.ready()) {
        LittleFS.remove(kTempFile);
        LittleFS.remove(kWorkFile);
        return result;
    }

    LittleFS.remove(kWorkFile);
    if (!LittleFS.rename(kTempFile, kWorkFile)) {
        LittleFS.remove(kTempFile);
        return result;
    }

    file_ = LittleFS.open(kWorkFile, FILE_APPEND);
    if (!file_) return result;

    std::snprintf(deviceId_, sizeof(deviceId_), "%s", header.deviceId);
    std::snprintf(firmware_, sizeof(firmware_), "%s", header.firmware);
    header_.deviceId = deviceId_;
    header_.firmware = firmware_;
    header_.corridorM = header.corridorM;

    // Naglowek juz w pliku jest — writer ma dopisywac delty wzgledem
    // odtworzonego punktu, a nie zaczynac od nowa.
    writer_.reset();
    writer_.resume(scanner.last());

    recording_ = true;
    open_ = true;
    fill_ = 0;
    points_ = scanner.points();
    bytes_ = kept;

    result.ok = true;
    result.last = scanner.last();
    result.timed = scanner.timed();
    result.points = scanner.points();
    return result;
}

bool TrackLogger::nextPending(uint32_t& seq) const {
    if (!mounted_) return false;
    uint32_t lowest = 0;
    if (countTracks(lowest) == 0) return false;
    seq = lowest;
    return true;
}

size_t TrackLogger::pendingCount() const {
    if (!mounted_) return 0;
    uint32_t lowest = 0;
    return countTracks(lowest);
}

File TrackLogger::openTrack(uint32_t seq) const {
    char name[24];
    trackName(name, sizeof(name), seq);
    return LittleFS.open(name, FILE_READ);
}

bool TrackLogger::removeTrack(uint32_t seq) {
    char name[24];
    trackName(name, sizeof(name), seq);
    return LittleFS.remove(name);
}

void TrackLogger::startRide(const track::TrackHeader& header) {
    if (!mounted_) return;
    // Nowy przejazd zaczyna od czystego pliku: cokolwiek zostalo, nalezy
    // do poprzedniego i albo dostalo juz numer, albo jest sierota.
    abortRide();

    std::snprintf(deviceId_, sizeof(deviceId_), "%s", header.deviceId);
    std::snprintf(firmware_, sizeof(firmware_), "%s", header.firmware);
    header_.deviceId = deviceId_;
    header_.firmware = firmware_;
    header_.corridorM = header.corridorM;

    writer_.reset();
    recording_ = true;
    open_ = false;
    fill_ = 0;
    points_ = 0;
    bytes_ = 0;
}

void TrackLogger::openFile() {
    makeRoom();
    file_ = LittleFS.open(kWorkFile, FILE_WRITE);
    open_ = static_cast<bool>(file_);
    if (!open_) recording_ = false;  // nie ma dokad pisac — nie udajemy, ze piszemy
}

void TrackLogger::write(const track::Point& point, uint32_t nowMs) {
    if (!recording_) return;

    if (!open_) {
        openFile();
        if (!open_) return;
        lastFlushMs_ = nowMs;
    }

    // Naglewek plus linia punktu; z zapasem na znacznik przerwy.
    char line[128];
    const size_t length = writer_.started() ? writer_.append(point, line, sizeof(line))
                                            : writer_.begin(header_, point, line, sizeof(line));
    if (length == 0) return;

    if (fill_ + length > sizeof(buffer_)) flushBuffer();
    if (fill_ + length > sizeof(buffer_)) return;  // linia dluzsza niz bufor — nie moze sie zdarzyc

    std::memcpy(buffer_ + fill_, line, length);
    fill_ += length;

    ++points_;
    bytes_ += static_cast<uint32_t>(length);

    tick(nowMs);
}

void TrackLogger::tick(uint32_t nowMs) {
    if (!open_ || fill_ == 0) return;
    if (nowMs - lastFlushMs_ < kFlushIntervalMs) return;

    flushBuffer();
    // Dopiero to naprawde odklada dane na flash. Bez tego zanik zasilania
    // kosztowalby cala zawartosc bufora systemu plikow, a nie tylko naszego.
    file_.flush();
    lastFlushMs_ = nowMs;
}

void TrackLogger::flushBuffer() {
    if (fill_ == 0 || !open_) return;
    file_.write(buffer_, fill_);
    fill_ = 0;
}

bool TrackLogger::finishRide(uint32_t seq) {
    if (!mounted_) return false;

    if (open_) {
        flushBuffer();
        file_.close();
        open_ = false;
    }

    recording_ = false;
    writer_.reset();

    // O tym, czy jest co domykac, decyduje ISTNIENIE PLIKU, a nie to, czy
    // logger akurat pisal. Przy starcie z zasilaniem urzadzenie archiwizuje
    // przejazd sprzed restartu — jego slad lezy wtedy na flashu, a logger
    // dopiero co wstal i niczego jeszcze nie zapisal.
    if (!LittleFS.exists(kWorkFile)) return false;

    char name[24];
    trackName(name, sizeof(name), seq);
    // Powtorna archiwizacja tego samego numeru nadpisuje — inaczej rename
    // odbilby sie o istniejacy plik i slad zostalby w pliku roboczym.
    if (LittleFS.exists(name)) LittleFS.remove(name);

    return LittleFS.rename(kWorkFile, name);
}

void TrackLogger::abortRide() {
    abortRideState();
    discardWorkFile();
}

void TrackLogger::abortRideState() {
    if (open_) {
        file_.close();
        open_ = false;
    }
    recording_ = false;
    writer_.reset();
    fill_ = 0;
    points_ = 0;
    bytes_ = 0;
}

void TrackLogger::makeRoom() {
    if (!mounted_) return;

    // Dwa warunki, nie jeden: liczba sladow ma odpowiadac historii przejazdow,
    // a miejsce ma wystarczyc na najdluzszy realny przejazd. Sam limit sztuk
    // nie chroni przed partycja zapchana przez RawLogger.
    for (;;) {
        uint32_t lowest = 0;
        const size_t count = countTracks(lowest);
        const bool tooMany = count > kMaxTracks;
        const bool tooTight = LittleFS.totalBytes() - LittleFS.usedBytes() < kReserveBytes;

        if (!tooMany && !tooTight) return;
        if (count == 0) return;  // nie ma juz czego usuwac

        char name[24];
        trackName(name, sizeof(name), lowest);
        if (!LittleFS.remove(name)) return;
    }
}

bool TrackLogger::handleCommand(Stream& io, const char* command) {
    if (std::strncmp(command, "SLADY", 5) != 0 && std::strncmp(command, "slady", 5) != 0) {
        return false;
    }

    if (!mounted_) {
        io.println("[slad] partycja niedostepna");
        return true;
    }

    const char* argument = command + 5;
    while (*argument == ' ') ++argument;

    if (*argument == '\0') {
        io.println("[slad] zapisane slady:");
        File root = LittleFS.open("/");
        size_t count = 0;
        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            uint32_t seq = 0;
            if (!parseSeq(entry.name(), seq)) continue;
            io.printf("  przejazd %lu: %lu B\n", static_cast<unsigned long>(seq),
                      static_cast<unsigned long>(entry.size()));
            ++count;
        }
        if (count == 0) io.println("  (brak)");
        io.printf("[slad] wolne miejsce: %lu z %lu B\n",
                  static_cast<unsigned long>(LittleFS.totalBytes() - LittleFS.usedBytes()),
                  static_cast<unsigned long>(LittleFS.totalBytes()));
        return true;
    }

    if (*argument == 'X' || *argument == 'x') {
        size_t removed = 0;
        for (;;) {
            uint32_t lowest = 0;
            if (countTracks(lowest) == 0) break;
            char name[24];
            trackName(name, sizeof(name), lowest);
            if (!LittleFS.remove(name)) break;
            ++removed;
        }
        io.printf("[slad] skasowane slady: %u\n", static_cast<unsigned>(removed));
        return true;
    }

    const uint32_t seq = static_cast<uint32_t>(std::strtoul(argument, nullptr, 10));
    char name[24];
    trackName(name, sizeof(name), seq);

    File file = LittleFS.open(name, FILE_READ);
    if (!file) {
        io.printf("[slad] nie ma sladu przejazdu %lu\n", static_cast<unsigned long>(seq));
        return true;
    }

    // Zrzut idzie surowy, bez ozdobnikow: to jest DOKLADNIE to, co poleci
    // w ciele zadania, wiec da sie go wkleic do pliku i sprawdzic curl-em.
    io.printf("[slad] przejazd %lu, %lu B:\n", static_cast<unsigned long>(seq),
              static_cast<unsigned long>(file.size()));
    while (file.available()) {
        io.write(static_cast<uint8_t>(file.read()));
    }
    file.close();
    io.println("[slad] koniec zrzutu");
    return true;
}

}  // namespace tracklog
