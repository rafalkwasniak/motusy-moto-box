#include "TimeWeighting.h"

#include <cmath>

namespace noise {

void TimeWeighting::configure(float tauS, float sampleRateHz) {
    meanSquare_ = 0.0f;

    if (tauS <= 0.0f || sampleRateHz <= 0.0f) {
        // Zerowa stala czasowa znaczy "bez wygladzania" — alfa = 1 przepuszcza
        // probke wprost. Lepsze niz dzielenie przez zero i cicha NaN-owa zaraza
        // w calym torze.
        alpha_ = 1.0f;
        return;
    }

    const double period = 1.0 / static_cast<double>(sampleRateHz);
    alpha_ = static_cast<float>(1.0 - std::exp(-period / static_cast<double>(tauS)));
}

float TimeWeighting::toDb(float meanSquare) {
    if (meanSquare <= 0.0f) return kMinDb;
    const float db = 10.0f * std::log10(meanSquare);
    return db < kMinDb ? kMinDb : db;
}

}  // namespace noise
