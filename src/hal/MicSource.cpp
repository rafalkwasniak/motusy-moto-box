#include "MicSource.h"

#include <M5Unified.h>

#include <cmath>

namespace hal {
namespace {

/// ES8311 na wewnetrznej magistrali (SDA=47, SCL=48).
constexpr uint8_t kEs8311Address = 0x18;
constexpr uint32_t kI2cFreq = 100000;

/// ADC_VOLUME — cyfrowe wzmocnienie toru wejsciowego, krok 0,5 dB.
constexpr uint8_t kRegAdcVolume = 0x17;

/// ALC. MUSI byc zerem: automatyczna regulacja wzmocnienia cicho zmienia
/// nachylenie charakterystyki i jest jedyna rzecza zdolna zepsuc
/// powtarzalnosc BEZ SLADU w ktorymkolwiek liczniku. To ta sama pulapka,
/// ktora zniszczyla pomiary na Raspberry Pi — patrz docs/noice.md §2.
constexpr uint8_t kRegAlc = 0x18;

/// Ile probek bierzemy na rozpoznanie kanalu. 1024 ramki to ~21 ms — dosc,
/// zeby odroznic sygnal od cyfrowej ciszy, i za malo, zeby ktokolwiek
/// zauwazyl to przy starcie.
constexpr size_t kProbeFrames = 1024;

float rmsDb(const int16_t* samples, size_t count, size_t stride, size_t offset) {
    if (count == 0) return noise::NoiseMeter::kNoData;

    double sum = 0.0;
    size_t used = 0;
    for (size_t i = offset; i < count; i += stride) {
        const double x = static_cast<double>(samples[i]) / 32768.0;
        sum += x * x;
        ++used;
    }
    if (used == 0) return noise::NoiseMeter::kNoData;

    const double meanSquare = sum / static_cast<double>(used);
    if (meanSquare <= 0.0) return noise::TimeWeighting::kMinDb;
    return static_cast<float>(10.0 * std::log10(meanSquare));
}

}  // namespace

bool MicSource::begin(const noise::NoiseMeterConfig& meterConfig) {
    if (taskHandle_ != nullptr) return true;
    if (!M5.Mic.isEnabled()) return false;

    meter_.configure(meterConfig);

    auto micConfig = M5.Mic.config();
    micConfig.sample_rate = config_.sampleRateHz;
    // Zdejmujemy wszystko, co M5Unified dokłada pod mowe — uzasadnienie
    // w naglowku MicSource.h.
    micConfig.magnification = 1;
    micConfig.over_sampling = 1;
    micConfig.noise_filter_level = 0;
    micConfig.stereo = false;
    micConfig.left_channel = config_.useLeftChannel;
    // Rdzen 0: petla Arduino zyje na 1, a audio nie moze czekac na rysowanie
    // ekranu ani na parsowanie NMEA.
    micConfig.task_pinned_core = 0;
    M5.Mic.config(micConfig);

    if (!M5.Mic.begin()) return false;

    // Rejestry piszemy PO begin(): wlasnie tam M5Unified wysyla swoja
    // sekwencje i nadpisalby nasze ustawienia.
    applyCodecRegisters();

    probeChannels();

    for (size_t i = 0; i < kBuffers; ++i) {
        if (buffers_[i] == nullptr) {
            buffers_[i] = static_cast<int16_t*>(
                heap_caps_calloc(MicSourceConfig::kBlockSamples, sizeof(int16_t),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (buffers_[i] == nullptr) {
            M5.Mic.end();
            return false;
        }
    }

    blockUs_ = static_cast<uint32_t>(
        static_cast<uint64_t>(MicSourceConfig::kBlockSamples) * 1000000ULL /
        config_.sampleRateHz);
    lastBlockUs_ = 0;
    stopRequested_ = false;

    if (xTaskCreatePinnedToCore(&MicSource::taskEntry, "halas", 4096, this, 2, &taskHandle_,
                                0) != pdPASS) {
        taskHandle_ = nullptr;
        M5.Mic.end();
        return false;
    }
    return true;
}

void MicSource::applyCodecRegisters() {
    M5.In_I2C.writeRegister8(kEs8311Address, kRegAdcVolume, config_.adcVolume, kI2cFreq);
    M5.In_I2C.writeRegister8(kEs8311Address, kRegAlc, 0x00, kI2cFreq);
}

void MicSource::probeChannels() {
    // Dokumentacja M5Stack juz raz wprowadzila nas w blad nazewnictwem pinow
    // z perspektywy kodeka, wiec kanalu nie zakladamy — mierzymy go.
    // Jesli OBA kanaly sa idealnie ciche, czytamy pin glosnika.
    const size_t elements = kProbeFrames * 2;
    int16_t* probe = static_cast<int16_t*>(
        heap_caps_calloc(elements, sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (probe == nullptr) return;

    if (M5.Mic.record(probe, elements, config_.sampleRateHz, true)) {
        uint32_t guardMs = 0;
        while (M5.Mic.isRecording() && guardMs < 500) {
            delay(1);
            ++guardMs;
        }
        probeLeftDb_ = rmsDb(probe, elements, 2, 0);
        probeRightDb_ = rmsDb(probe, elements, 2, 1);

        // Kanal przelaczamy tylko przy roznicy NIEBUDZACEJ WATPLIWOSCI.
        // W ciszy oba kanaly leza na podlodze szumu i wybor bylby losowy,
        // a cicha zamiana kanalu miedzy uruchomieniami zerwalaby
        // powtarzalnosc sezonu skuteczniej niz zly kanal.
        constexpr float kDecisiveDb = 6.0f;
        if (probeLeftDb_ > probeRightDb_ + kDecisiveDb) {
            config_.useLeftChannel = true;
        } else if (probeRightDb_ > probeLeftDb_ + kDecisiveDb) {
            config_.useLeftChannel = false;
        }

        auto micConfig = M5.Mic.config();
        micConfig.left_channel = config_.useLeftChannel;
        M5.Mic.config(micConfig);
    }

    heap_caps_free(probe);
}

void MicSource::end() {
    if (taskHandle_ != nullptr) {
        stopRequested_ = true;
        // Zadanie sprawdza flage miedzy blokami, wiec czeka sie najwyzej
        // tyle, ile trwa jeden blok.
        uint32_t guardMs = 0;
        while (taskHandle_ != nullptr && guardMs < 200) {
            delay(1);
            ++guardMs;
        }
        taskHandle_ = nullptr;
    }

    M5.Mic.end();

    for (size_t i = 0; i < kBuffers; ++i) {
        if (buffers_[i] != nullptr) {
            heap_caps_free(buffers_[i]);
            buffers_[i] = nullptr;
        }
    }
}

void MicSource::taskEntry(void* arg) {
    static_cast<MicSource*>(arg)->taskLoop();
}

void MicSource::taskLoop() {
    size_t next = 0;
    size_t primed = 0;

    while (!stopRequested_) {
        if (!M5.Mic.record(buffers_[next], MicSourceConfig::kBlockSamples)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (primed + 1 < kBuffers) {
            ++primed;
        } else {
            // Bufor zakolejkowany jako przedostatni jest juz gotowy: dwa
            // sloty M5Unified sa zajete przez pozostale dwa bufory.
            const size_t done = (next + 1) % kBuffers;
            processBlock(buffers_[done], MicSourceConfig::kBlockSamples);
        }
        next = (next + 1) % kBuffers;
    }

    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

void MicSource::processBlock(const int16_t* samples, size_t count) {
    bool recording;
    float speedKmh;
    bool wantReset;
    portENTER_CRITICAL(&lock_);
    recording = pendingRecording_;
    speedKmh = pendingSpeedKmh_;
    wantReset = pendingReset_;
    pendingReset_ = false;
    portEXIT_CRITICAL(&lock_);

    if (wantReset) meter_.reset();
    meter_.setRecording(recording);
    meter_.setSpeedKmh(speedKmh);

    // Przerwa w strumieniu. Porownujemy odstep miedzy blokami z czasem
    // trwania bloku — interesuja nas wylacznie grube zatrzymania, wiec
    // dryf zegara kodeka wobec millis() nie ma tu znaczenia.
    const uint32_t nowUs = micros();
    if (lastBlockUs_ != 0 && blockUs_ != 0) {
        const uint32_t gapUs = nowUs - lastBlockUs_;
        if (gapUs > blockUs_ * 3) {
            const uint64_t lost =
                static_cast<uint64_t>(gapUs - blockUs_) * config_.sampleRateHz / 1000000ULL;
            meter_.addDropped(static_cast<uint32_t>(lost));
        }
    }
    lastBlockUs_ = nowUs;

    for (size_t i = 0; i < count; ++i) {
        meter_.addSample(static_cast<float>(samples[i]) / 32768.0f);
    }

    MicSnapshot fresh;
    fresh.maxNoiseDb = meter_.maxNoiseDb();
    fresh.maxNoiseSpeedKmh = meter_.maxNoiseSpeedKmh();
    fresh.currentDb = meter_.currentDb();
    fresh.instantDb = meter_.instantDb();
    fresh.clipped = meter_.clippedCount();
    fresh.dropped = meter_.droppedCount();
    fresh.ready = meter_.ready();

    portENTER_CRITICAL(&lock_);
    published_ = fresh;
    ++blocks_;
    portEXIT_CRITICAL(&lock_);
}

void MicSource::setRecording(bool recording) {
    portENTER_CRITICAL(&lock_);
    pendingRecording_ = recording;
    portEXIT_CRITICAL(&lock_);
}

void MicSource::setSpeedKmh(float kmh) {
    portENTER_CRITICAL(&lock_);
    pendingSpeedKmh_ = kmh;
    portEXIT_CRITICAL(&lock_);
}

void MicSource::resetRide() {
    portENTER_CRITICAL(&lock_);
    pendingReset_ = true;
    published_ = MicSnapshot{};
    portEXIT_CRITICAL(&lock_);
}

MicSnapshot MicSource::snapshot() const {
    MicSnapshot copy;
    portENTER_CRITICAL(&lock_);
    copy = published_;
    portEXIT_CRITICAL(&lock_);
    return copy;
}

uint32_t MicSource::blocks() const {
    uint32_t value;
    portENTER_CRITICAL(&lock_);
    value = blocks_;
    portEXIT_CRITICAL(&lock_);
    return value;
}

}  // namespace hal
