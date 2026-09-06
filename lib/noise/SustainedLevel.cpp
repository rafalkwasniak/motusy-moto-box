#include "SustainedLevel.h"

namespace noise {

void SustainedLevel::configure(float windowSec, float stepMs, float tolerancePercent) {
    size_t wanted = kMaxSamples;
    if (windowSec > 0.0f && stepMs > 0.0f) {
        const float samples = windowSec * 1000.0f / stepMs;
        wanted = static_cast<size_t>(samples + 0.5f);
    }
    if (wanted < 1) wanted = 1;
    if (wanted > kMaxSamples) wanted = kMaxSamples;
    capacity_ = wanted;

    if (tolerancePercent < 1.0f) tolerancePercent = 1.0f;
    if (tolerancePercent > 100.0f) tolerancePercent = 100.0f;
    tolerance_ = tolerancePercent;

    reset();
}

void SustainedLevel::reset() {
    for (size_t i = 0; i < kBins; ++i) bins_[i] = 0;
    head_ = 0;
    filled_ = 0;
}

size_t SustainedLevel::binFor(float levelDb) const {
    if (levelDb <= kMinDb) return 0;
    const float offset = (levelDb - kMinDb) / kBinWidthDb;
    if (offset >= static_cast<float>(kBins - 1)) return kBins - 1;
    return static_cast<size_t>(offset);
}

void SustainedLevel::addLevel(float levelDb) {
    const size_t index = binFor(levelDb);

    if (filled_ == capacity_) {
        --bins_[ring_[head_]];  // okno pelne — najstarsza probka wypada
    } else {
        ++filled_;
    }

    ring_[head_] = static_cast<uint16_t>(index);
    head_ = (head_ + 1) % capacity_;
    ++bins_[index];
}

float SustainedLevel::currentDb() const {
    if (!ready()) return kNoData;

    // Idziemy od NAJGLOSNIEJSZYCH koszykow w dol, az uzbieramy `tolerance_`
    // procent okna. Poziom, na ktorym sie zatrzymamy, jest tym utrzymanym.
    // Kierunek jest wazny: pytamy "jaki poziom byl przekroczony przez 90 %
    // czasu", wiec 90 % daje wartosc NISKA. To ta sama operacja co LA90,
    // tylko liczona w oknie 5 s zamiast w minucie.
    const double target = static_cast<double>(capacity_) * tolerance_ / 100.0;

    double sum = 0.0;
    for (size_t i = kBins; i-- > 0;) {
        sum += bins_[i];
        if (sum >= target) {
            return kMinDb + (static_cast<float>(i) + 0.5f) * kBinWidthDb;
        }
    }
    return kMinDb;
}

}  // namespace noise
