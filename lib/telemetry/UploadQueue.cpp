#include "UploadQueue.h"

namespace telemetry {

// Przesylka miesci dokladnie tyle, ile pamieta urzadzenie. Gdyby historia
// urosla, a limit przesylki nie, najstarsze przejazdy nigdy by nie wyjechaly.
static_assert(kMaxRidesPerPayload == motion::RideHistory::kCapacity,
              "Limit przesylki musi odpowiadac pojemnosci historii");

void UploadQueue::restore(uint32_t lastSeq, uint32_t sentThrough) {
    lastSeq_ = lastSeq;
    sentThrough_ = sentThrough > lastSeq ? lastSeq : sentThrough;
}

uint32_t UploadQueue::onRideArchived() {
    ++lastSeq_;
    return lastSeq_;
}

size_t UploadQueue::pendingCount(size_t historyCount) const {
    if (lastSeq_ <= sentThrough_) return 0;

    const uint32_t unsent = lastSeq_ - sentThrough_;
    // Przejazdy, ktore wypadly z historii, sa nie do odzyskania — nie ma sensu
    // ich liczyc jako czekajace.
    if (unsent > historyCount) return historyCount;
    return static_cast<size_t>(unsent);
}

uint32_t UploadQueue::seqAt(size_t historyIndex, size_t historyCount) const {
    if (historyIndex >= historyCount) return 0;
    if (static_cast<uint32_t>(historyIndex) >= lastSeq_) return 0;
    return lastSeq_ - static_cast<uint32_t>(historyIndex);
}

size_t UploadQueue::collect(const motion::RideHistory& history, RideRecord* out,
                            size_t maxOut) const {
    if (out == nullptr || maxOut == 0) return 0;

    const size_t pending = pendingCount(history.count());
    const size_t take = pending > maxOut ? maxOut : pending;
    if (take == 0) return 0;

    // Pozycja `pending - 1` to NAJSTARSZY zalegly przejazd; idziemy od niego
    // w strone najnowszego, czyli numerami rosnaco. Gdy przesylka nie miesci
    // wszystkiego, urywamy ja od strony najnowszych — zaleglosc nadrabia sie
    // od poczatku, bez dziury w srodku.
    size_t written = 0;
    for (size_t i = pending; i > pending - take; --i) {
        const size_t index = i - 1;
        RideRecord& record = out[written];
        record = RideRecord{};
        record.seq = seqAt(index, history.count());
        record.values = history.at(index);
        ++written;
    }
    return written;
}

bool UploadQueue::confirmSentThrough(uint32_t seq) {
    if (seq <= sentThrough_) return false;
    // Serwer nie moze potwierdzic przejazdu, ktorego urzadzenie nie wyslalo.
    // Przyjecie takiej liczby cicho skasowaloby kolejke.
    if (seq > lastSeq_) return false;

    sentThrough_ = seq;
    return true;
}

}  // namespace telemetry
