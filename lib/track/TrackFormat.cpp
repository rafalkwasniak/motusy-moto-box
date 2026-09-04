#include "TrackFormat.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace track {

void TrackWriter::reset() {
    previous_ = Point{};
    started_ = false;
}

size_t TrackWriter::begin(const TrackHeader& header, const Point& first, char* out,
                          size_t outSize) {
    if (out == nullptr || outSize == 0) return 0;

    const int written = std::snprintf(out, outSize,
                                      "%s\n"
                                      "dev=%s\n"
                                      "fw=%s\n"
                                      "eps=%u\n"
                                      "t0=%lu\n"
                                      "p0=%ld,%ld\n"
                                      "\n",
                                      kFormatMagic, header.deviceId, header.firmware,
                                      static_cast<unsigned>(header.corridorM),
                                      static_cast<unsigned long>(first.timeS),
                                      static_cast<long>(first.lonE5),
                                      static_cast<long>(first.latE5));

    // snprintf obcina i zwraca dlugosc, ktora BYLABY potrzebna — obciety
    // naglowek jest gorszy niz jego brak, bo plik wyglada na poprawny.
    if (written < 0 || static_cast<size_t>(written) >= outSize) {
        out[0] = '\0';
        return 0;
    }

    previous_ = first;
    started_ = true;
    return static_cast<size_t>(written);
}

size_t TrackWriter::append(const Point& point, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0) return 0;
    // Bez naglowka nie ma od czego liczyc delty. Cichy zapis dalby plik,
    // ktorego pierwsza linia jest delta wzgledem zera, czyli slad w Zatoce
    // Gwinejskiej.
    if (!started_) {
        out[0] = '\0';
        return 0;
    }

    size_t used = 0;

    if (point.startsSegment) {
        // Znacznik przerwy: tunel, wiadukt, garaz, postoj. Bez niego mapa
        // narysuje prosta przez pol miasta. Delty PLYNA DALEJ przez przerwe —
        // znacznik mowi tylko "podnies olowek", a nie "zacznij liczyc od nowa".
        if (outSize < 3) {
            out[0] = '\0';
            return 0;
        }
        out[used++] = '-';
        out[used++] = '\n';
    }

    const int written = std::snprintf(out + used, outSize - used, "%ld,%ld,%lu,%d\n",
                                      static_cast<long>(point.lonE5 - previous_.lonE5),
                                      static_cast<long>(point.latE5 - previous_.latE5),
                                      static_cast<unsigned long>(point.timeS - previous_.timeS),
                                      static_cast<int>(point.leanDeg));

    if (written < 0 || static_cast<size_t>(written) >= outSize - used) {
        out[0] = '\0';
        return 0;
    }

    previous_ = point;
    return used + static_cast<size_t>(written);
}

void TrackWriter::resume(const Point& last) {
    previous_ = last;
    started_ = true;
}

namespace {

/// Czyta dokladnie `count` liczb calkowitych rozdzielonych przecinkami.
/// Nadmiar pol, brak pola albo znak spoza cyfr konczy sie odmowa — plik sladu
/// ma sztywny uklad, wiec tolerancja tylko ukrylaby uszkodzenie.
bool parseIntegers(const char* text, long* out, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (*text == '\0') return false;

        char* end = nullptr;
        out[i] = std::strtol(text, &end, 10);
        if (end == text) return false;

        text = end;
        if (i + 1 < count) {
            if (*text != ',') return false;
            ++text;
        }
    }
    return *text == '\0';
}

}  // namespace

void TrackScanner::reset() {
    sawMagic_ = false;
    inHeader_ = true;
    ready_ = false;
    timed_ = false;
    sawStart_ = false;
    startLon_ = 0;
    startLat_ = 0;
    startTime_ = 0;
    last_ = Point{};
    points_ = 0;
}

bool TrackScanner::feedLine(const char* line) {
    if (!sawMagic_) {
        if (std::strcmp(line, kFormatMagic) != 0) return false;
        sawMagic_ = true;
        return true;
    }

    if (inHeader_) {
        // Pusta linia konczy naglowek. Bez punktu startowego nie ma od czego
        // liczyc delt, wiec taki plik jest bezuzyteczny.
        if (line[0] == '\0') {
            inHeader_ = false;
            if (!sawStart_) return false;
            last_.lonE5 = startLon_;
            last_.latE5 = startLat_;
            last_.timeS = startTime_;
            ready_ = true;
            return true;
        }

        const char* equals = std::strchr(line, '=');
        if (equals == nullptr) return false;

        const size_t keyLength = static_cast<size_t>(equals - line);
        const char* value = equals + 1;

        if (keyLength == 2 && std::strncmp(line, "t0", 2) == 0) {
            long parsed = 0;
            if (!parseIntegers(value, &parsed, 1) || parsed < 0) return false;
            startTime_ = static_cast<uint32_t>(parsed);
            timed_ = parsed != 0;
        } else if (keyLength == 2 && std::strncmp(line, "p0", 2) == 0) {
            long parsed[2] = {0, 0};
            if (!parseIntegers(value, parsed, 2)) return false;
            startLon_ = static_cast<int32_t>(parsed[0]);
            startLat_ = static_cast<int32_t>(parsed[1]);
            sawStart_ = true;
        }
        // Nieznane klucze przechodza — naglowek moze urosnac bez zmiany wersji.
        return true;
    }

    if (!ready_) return false;

    // Znacznik przerwy nie niesie wspolrzednych i nie rusza sumowania.
    if (std::strcmp(line, "-") == 0) return true;

    long parsed[4] = {0, 0, 0, 0};
    if (!parseIntegers(line, parsed, 4)) return false;
    if (parsed[2] < 0) return false;  // czas nie ma prawa sie cofac

    last_.lonE5 += static_cast<int32_t>(parsed[0]);
    last_.latE5 += static_cast<int32_t>(parsed[1]);
    last_.timeS += static_cast<uint32_t>(parsed[2]);
    last_.leanDeg = static_cast<int8_t>(parsed[3]);
    ++points_;
    return true;
}

}  // namespace track
