#include "UploadScheduler.h"

namespace telemetry {

bool UploadScheduler::shouldAttempt(bool configured, bool hasPending, bool radioAllowed,
                                    uint32_t nowMs) const {
    if (!configured || !hasPending || !radioAllowed) return false;
    if (blocked_) return false;
    return msUntilNextAttempt(nowMs) == 0;
}

uint32_t UploadScheduler::msUntilNextAttempt(uint32_t nowMs) const {
    if (!waiting_) return 0;

    // Roznica na liczbach ze znakiem: millis() przekreca sie po 49 dniach,
    // a odejmowanie modulo 2^32 daje wtedy poprawny wynik, podczas gdy
    // porownanie "nowMs < nextAttemptMs_" zatrzymaloby wysylke na zawsze.
    const int32_t remaining = static_cast<int32_t>(nextAttemptMs_ - nowMs);
    return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

void UploadScheduler::onOutcome(UploadOutcome outcome, uint32_t nowMs) {
    switch (outcome) {
        case UploadOutcome::Success:
            failures_ = 0;
            waiting_ = false;
            break;

        case UploadOutcome::AuthRejected:
            // Nie ustawiamy odstepu — po odmowie nie ma "nastepnej proby"
            // do czasu, az uzytkownik zmieni konfiguracje.
            blocked_ = true;
            waiting_ = false;
            break;

        case UploadOutcome::TemporaryFailure: {
            ++failures_;

            uint32_t delay = cfg_.firstBackoffMs;
            for (uint32_t i = 1; i < failures_ && delay < cfg_.maxBackoffMs; ++i) {
                delay *= 2;
            }
            if (delay > cfg_.maxBackoffMs) delay = cfg_.maxBackoffMs;

            nextAttemptMs_ = nowMs + delay;
            waiting_ = true;
            break;
        }
    }
}

void UploadScheduler::onConfigChanged() {
    blocked_ = false;
    waiting_ = false;
    failures_ = 0;
}

}  // namespace telemetry
