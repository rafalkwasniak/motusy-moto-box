#include "TrackDecimator.h"

#include <cmath>

namespace track {
namespace {

/// Metry na jednostke 1e-5 stopnia szerokosci: poludnik ma 111 320 m na stopien,
/// czyli 1,1132 m na jednostke. Rozdzielczosc 1e-5 wybrana swiadomie — GPS ma
/// blad 2-5 m, wiec szosta cyfra opisywalaby wylacznie szum, a kosztowala jedna
/// cyfre w KAZDEJ delcie, czyli ~20% calego sladu.
constexpr float kMetersPerUnitLat = 1.1132f;

constexpr float kPi = 3.14159265f;

/// Maksimum co do wartosci bezwzglednej, ale ZE ZNAKIEM — strona zakretu jest
/// tak samo istotna jak kat, wiec zwykle std::max daloby bzdure dla lewych.
int8_t maxLean(int8_t a, int8_t b) {
    const int av = a < 0 ? -a : a;
    const int bv = b < 0 ? -b : b;
    return av >= bv ? a : b;
}

}  // namespace

void TrackDecimator::reset() {
    haveAnchor_ = false;
    pendingBreak_ = true;
    count_ = 0;
    metersPerUnitLon_ = 0.0f;
}

void TrackDecimator::breakSegment() {
    // Przelicznik dlugosci NIE jest tu zerowany: przerwa w sladzie nie konczy
    // przejazdu, a liczymy go raz na przejazd.
    haveAnchor_ = false;
    pendingBreak_ = true;
    count_ = 0;
}

bool TrackDecimator::update(const Fix& fix, Point& out) {
    if (!haveAnchor_) {
        if (metersPerUnitLon_ <= 0.0f) {
            const float latDeg = static_cast<float>(fix.latE5) / 100000.0f;
            metersPerUnitLon_ = kMetersPerUnitLat * std::cos(latDeg * kPi / 180.0f);
            // Blisko biegunow cosinus dazy do zera i caly swiat zlalby sie
            // w jeden punkt. Podloga jest tu wylacznie po to, zeby zle dane
            // dawaly slad brzydki, a nie dzielenie przez zero.
            if (metersPerUnitLon_ < 0.01f) metersPerUnitLon_ = 0.01f;
        }

        anchor_ = fix;
        haveAnchor_ = true;
        count_ = 0;

        out.lonE5 = fix.lonE5;
        out.latE5 = fix.latE5;
        out.timeS = fix.timeS;
        out.leanDeg = fix.leanDeg;
        out.startsSegment = pendingBreak_;
        pendingBreak_ = false;
        return true;
    }

    // Nowy fix jest KONCEM sprawdzanego odcinka, a nie jego czescia — dlatego
    // testujemy bufor wzgledem A->fix, a do bufora fix trafia dopiero pozniej.
    // Ta kolejnosc daje gwarancje korytarza opisana w naglowku.
    bool violated = false;
    for (size_t i = 0; i < count_; ++i) {
        if (deviationM(buffer_[i], fix) > config_.corridorM) {
            violated = true;
            break;
        }
    }

    const bool tooLong = fix.timeS - anchor_.timeS >= config_.maxGapS;
    const bool full = count_ >= kMaxBuffer;

    if ((violated || tooLong || full) && count_ > 0) {
        const size_t last = count_ - 1;
        // Ogranicznik z gory. Gdy kandydat lezy zbyt blisko kotwicy w czasie,
        // zapisu NIE robimy — korytarz degraduje sie wtedy do rozdzielczosci
        // `minGapS` i dokladnie to ten ogranicznik znaczy.
        if (buffer_[last].timeS - anchor_.timeS >= config_.minGapS) {
            emitAt(last, out);
            buffer_[0] = fix;
            count_ = 1;
            return true;
        }
    }

    if (count_ < kMaxBuffer) buffer_[count_++] = fix;
    return false;
}

bool TrackDecimator::flush(Point& out) {
    if (count_ == 0) return false;
    emitAt(count_ - 1, out);
    count_ = 0;
    return true;
}

void TrackDecimator::emitAt(size_t index, Point& out) {
    // Przechyl punktu to maksimum z CALEGO odcinka, ktory ten punkt zastepuje —
    // razem z fixami wlasnie odrzucanymi. Inaczej najciekawszy kat gubilby sie
    // dokladnie tam, gdzie decymator dziala najmocniej.
    int8_t lean = buffer_[0].leanDeg;
    for (size_t i = 1; i <= index; ++i) lean = maxLean(lean, buffer_[i].leanDeg);

    const Fix& point = buffer_[index];
    out.lonE5 = point.lonE5;
    out.latE5 = point.latE5;
    out.timeS = point.timeS;
    out.leanDeg = lean;
    // Segment zaczyna wylacznie punkt po przerwie, a ten idzie druga sciezka
    // (galaz bez kotwicy). Tutaj zawsze jest ciag dalszy.
    out.startsSegment = false;

    anchor_ = point;
}

float TrackDecimator::deviationM(const Fix& point, const Fix& to) const {
    const float ax = static_cast<float>(point.lonE5 - anchor_.lonE5) * metersPerUnitLon_;
    const float ay = static_cast<float>(point.latE5 - anchor_.latE5) * kMetersPerUnitLat;
    const float bx = static_cast<float>(to.lonE5 - anchor_.lonE5) * metersPerUnitLon_;
    const float by = static_cast<float>(to.latE5 - anchor_.latE5) * kMetersPerUnitLat;

    const float len2 = bx * bx + by * by;
    if (len2 <= 0.0f) return std::sqrt(ax * ax + ay * ay);

    // Rzut przyciety do koncow odcinka: slad jest lamana, wiec mierzymy
    // odleglosc od ODCINKA, nie od nieskonczonej prostej przez niego.
    float t = (ax * bx + ay * by) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    const float dx = ax - t * bx;
    const float dy = ay - t * by;
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace track
