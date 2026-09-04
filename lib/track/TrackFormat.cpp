#include "TrackFormat.h"

#include <cstdio>

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

}  // namespace track
