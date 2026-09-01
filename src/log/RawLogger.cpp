#include "RawLogger.h"

#if MMB_RAW_LOGGER

#include <LittleFS.h>

#include <cstdio>
#include <cstring>

namespace rawlog {
namespace {

constexpr const char* kPartitionLabel = "storage";
constexpr const char* kMagic = "MMB1";
constexpr uint32_t kFormatVersion = 1;

/// Nazwa pliku sesji: /log_007.bin
void fileName(char* out, size_t size, unsigned number) {
    std::snprintf(out, size, "/log_%03u.bin", number);
}

bool parseNumber(const char* name, unsigned& out) {
    unsigned value = 0;
    if (std::sscanf(name, "log_%u.bin", &value) == 1 ||
        std::sscanf(name, "/log_%u.bin", &value) == 1) {
        out = value;
        return true;
    }
    return false;
}

}  // namespace

bool RawLogger::begin() {
    // Pierwsze uruchomienie po zmianie tabeli partycji zastaje smieci po
    // dawnym slocie aplikacji — stad formatOnFail.
    mounted_ = LittleFS.begin(true, "/littlefs", 10, kPartitionLabel);
    return mounted_;
}

unsigned RawLogger::nextFileNumber() {
    unsigned highest = 0;
    File root = LittleFS.open("/");
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        unsigned number = 0;
        if (parseNumber(entry.name(), number) && number > highest) highest = number;
    }
    return highest + 1;
}

void RawLogger::freeSpaceFor(size_t requiredBytes) {
    // Usuwaj najstarsze pliki, az bedzie miejsce na nowa sesje.
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < requiredBytes) {
        unsigned lowest = 0;
        bool found = false;
        File root = LittleFS.open("/");
        for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
            unsigned number = 0;
            if (!parseNumber(entry.name(), number)) continue;
            if (!found || number < lowest) {
                lowest = number;
                found = true;
            }
        }
        if (!found) return;  // nie ma juz czego usuwac

        char name[24];
        fileName(name, sizeof(name), lowest);
        if (!LittleFS.remove(name)) return;
    }
}

void RawLogger::startSession(uint32_t nowMs) {
    if (!mounted_) return;
    stopSession();

    freeSpaceFor(kSessionReserveBytes);

    char name[24];
    fileName(name, sizeof(name), nextFileNumber());
    file_ = LittleFS.open(name, FILE_WRITE);
    if (!file_) return;

    file_.write(reinterpret_cast<const uint8_t*>(kMagic), 4);
    uint32_t version = kFormatVersion;
    file_.write(reinterpret_cast<const uint8_t*>(&version), 4);

    fill_ = 0;
    active_ = true;
    full_ = false;
    lastSampleMs_ = 0;
    lastFlushMs_ = nowMs;
}

void RawLogger::stopSession() {
    if (!active_) return;
    flushBuffer();
    file_.close();
    active_ = false;
}

void RawLogger::flushBuffer() {
    if (fill_ == 0) return;
    file_.write(buffer_, fill_);
    fill_ = 0;
}

void RawLogger::writeRecord(const motion::ImuSample& sample) {
    uint8_t* cursor = buffer_ + fill_;
    const uint32_t t = static_cast<uint32_t>(sample.timestampMs);
    std::memcpy(cursor, &t, 4);
    cursor += 4;

    const float values[6] = {sample.accelG.x,   sample.accelG.y,   sample.accelG.z,
                             sample.gyroRadS.x, sample.gyroRadS.y, sample.gyroRadS.z};
    std::memcpy(cursor, values, sizeof(values));
    fill_ += kRecordBytes;
}

void RawLogger::log(const motion::ImuSample& sample) {
    if (!active_) return;

    const uint32_t nowMs = static_cast<uint32_t>(sample.timestampMs);
    if (nowMs - lastSampleMs_ < kMinSampleGapMs) return;
    lastSampleMs_ = nowMs;

    writeRecord(sample);
    if (fill_ + kRecordBytes > kBufferBytes) flushBuffer();

    if (nowMs - lastFlushMs_ >= kFlushIntervalMs) {
        lastFlushMs_ = nowMs;
        flushBuffer();
        file_.flush();

        if (LittleFS.totalBytes() - LittleFS.usedBytes() < kStopMarginBytes) {
            // Partycja pelna: domykamy plik i przestajemy pisac. Pierwsza
            // czesc jazdy jest cenniejsza niz ryzyko uszkodzenia systemu plikow.
            stopSession();
            full_ = true;
        }
    }
}

void RawLogger::listFiles(Stream& io) {
    io.printf("[log] partycja: %u KB wolne z %u KB\n",
              static_cast<unsigned>((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024),
              static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    File root = LittleFS.open("/");
    for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        unsigned number = 0;
        if (!parseNumber(entry.name(), number)) continue;
        const size_t records = (entry.size() > 8 ? entry.size() - 8 : 0) / kRecordBytes;
        io.printf("[log] %u: %s  %u KB  ~%u s\n", number, entry.name(),
                  static_cast<unsigned>(entry.size() / 1024),
                  static_cast<unsigned>(records / 100));
    }
    io.printf("[log] aktywna sesja: %s%s\n", active_ ? "TAK" : "nie",
              full_ ? " (przerwana - pelna partycja)" : "");
}

void RawLogger::dumpFile(Stream& io, unsigned number) {
    char name[24];
    fileName(name, sizeof(name), number);
    File input = LittleFS.open(name, FILE_READ);
    if (!input) {
        io.printf("[log] brak pliku %s\n", name);
        return;
    }

    uint8_t header[8];
    if (input.read(header, 8) != 8 || std::memcmp(header, kMagic, 4) != 0) {
        io.printf("[log] %s: uszkodzony naglowek\n", name);
        return;
    }

    io.printf("=== %s ===\n", name);
    io.println("t_ms,ax_g,ay_g,az_g,gx_rads,gy_rads,gz_rads");

    uint8_t record[kRecordBytes];
    while (input.read(record, kRecordBytes) == static_cast<int>(kRecordBytes)) {
        uint32_t t = 0;
        float values[6];
        std::memcpy(&t, record, 4);
        std::memcpy(values, record + 4, sizeof(values));
        io.printf("%lu,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n", static_cast<unsigned long>(t),
                  static_cast<double>(values[0]), static_cast<double>(values[1]),
                  static_cast<double>(values[2]), static_cast<double>(values[3]),
                  static_cast<double>(values[4]), static_cast<double>(values[5]));
    }
    io.println("=== KONIEC ===");
}

void RawLogger::deleteAll(Stream& io) {
    stopSession();
    unsigned removed = 0;
    // Zbieramy numery przed usuwaniem — kasowanie w trakcie iteracji katalogu
    // potrafi gubic wpisy.
    unsigned numbers[64];
    size_t count = 0;
    File root = LittleFS.open("/");
    for (File entry = root.openNextFile(); entry && count < 64; entry = root.openNextFile()) {
        unsigned number = 0;
        if (parseNumber(entry.name(), number)) numbers[count++] = number;
    }
    for (size_t i = 0; i < count; ++i) {
        char name[24];
        fileName(name, sizeof(name), numbers[i]);
        if (LittleFS.remove(name)) ++removed;
    }
    io.printf("[log] usunieto %u plikow\n", removed);
}

bool RawLogger::handleCommand(Stream& io, const char* command) {
    if (command == nullptr) return false;

    const char letter = command[0];
    if (letter != 'L' && letter != 'D' && letter != 'X') return false;

    // Rozpoznanie litery jest niezalezne od stanu partycji: cisza w odpowiedzi
    // na wlasna komende wygladalaby jak zawieszone urzadzenie.
    if (!mounted_) {
        io.println("[log] partycja niedostepna");
        return true;
    }

    if (letter == 'L') {
        listFiles(io);
    } else if (letter == 'D') {
        unsigned number = 0;
        if (std::sscanf(command + 1, "%u", &number) == 1) {
            dumpFile(io, number);
        } else {
            io.println("[log] uzycie: D<nr pliku>");
        }
    } else {
        deleteAll(io);
    }
    return true;
}

}  // namespace rawlog

#endif  // MMB_RAW_LOGGER
