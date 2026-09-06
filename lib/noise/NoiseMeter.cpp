#include "NoiseMeter.h"

namespace noise {

void NoiseMeter::configure(const NoiseMeterConfig& config) {
    config_ = config;

    weighting_.configure(config_.sampleRateHz);
    fast_.configure(config_.fastTauS, config_.sampleRateHz);
    sustained_.configure(config_.windowSec, config_.stepMs, config_.tolerancePercent);

    // Ile probek przypada na jeden krok okna. Przy 48 kHz i 10 ms to 480.
    stepDivider_ = 1;
    if (config_.sampleRateHz > 0.0f && config_.stepMs > 0.0f) {
        const float samples = config_.sampleRateHz * config_.stepMs / 1000.0f;
        if (samples >= 1.0f) stepDivider_ = static_cast<uint32_t>(samples + 0.5f);
    }

    reset();
}

void NoiseMeter::reset() {
    weighting_.reset();
    fast_.reset();
    sustained_.reset();

    stepCounter_ = 0;
    armedSteps_ = 0;
    maxDb_ = kNoData;
    maxSpeedKmh_ = 0.0f;
    clipped_ = 0;
    dropped_ = 0;
}

void NoiseMeter::addSample(float x) {
    if (x >= kClipLevel || x <= -kClipLevel) ++clipped_;

    fast_.process(weighting_.process(x));

    if (++stepCounter_ < stepDivider_) return;
    stepCounter_ = 0;

    sustained_.addLevel(TimeWeighting::toDb(fast_.meanSquare()));

    if (!recording_) {
        armedSteps_ = 0;
        return;
    }

    // Okno musi w CALOSCI pochodzic z czasu, gdy bramka byla otwarta.
    // Samo `sustained_.ready()` nie wystarcza: okno jest przesuwne, wiec
    // zaraz po otwarciu bramki niesie jeszcze dzwiek sprzed niej — czyli
    // blip gazem na czerwonym swietle ustanawialby rekord przejazdu.
    // Kosztuje to pierwsze 5 s po kazdym otwarciu bramki i jest to koszt
    // przyjety swiadomie: pomiar ma pochodzic z jazdy, a nie z postoju.
    ++armedSteps_;
    if (armedSteps_ < sustained_.capacity()) return;

    const float level = sustained_.currentDb();
    if (maxDb_ == kNoData || level > maxDb_) {
        maxDb_ = level;
        maxSpeedKmh_ = speedKmh_;
    }
}

float NoiseMeter::maxNoiseDb() const {
    if (maxDb_ == kNoData) return kNoData;
    return maxDb_ + config_.calibrationDb;
}

float NoiseMeter::currentDb() const {
    const float level = sustained_.currentDb();
    if (level == kNoData) return kNoData;
    return level + config_.calibrationDb;
}

float NoiseMeter::instantDb() const {
    return TimeWeighting::toDb(fast_.meanSquare()) + config_.calibrationDb;
}

}  // namespace noise
