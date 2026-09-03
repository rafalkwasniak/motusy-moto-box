#include "GpsSource.h"

#include <M5Unified.h>

namespace hal {
namespace {

/// Predkosci transmisji sprawdzane po kolei; kazda na obu pinach, stad liczba
/// kombinacji razy dwa.
///
/// PIERWSZA JEST ZMIERZONA, NIE ZGADNIETA: egzemplarz z antena zewnetrzna
/// nadaje 115200 baud na G10 (sprawdzone 2026-09-03 — dokumentacja modulu
/// podaje 9600, wiec zgadywanie z niej konczylo sie cisza). Reszta zostaje
/// jako siatka bezpieczenstwa na wypadek innego egzemplarza albo modulu
/// przestawionego komenda.
constexpr uint32_t kBauds[] = {115200, 9600, 38400, 57600, 19200, 4800};
constexpr uint8_t kProbeCount = static_cast<uint8_t>(sizeof(kBauds) / sizeof(kBauds[0]) * 2);

}  // namespace

void GpsSource::begin(uint32_t nowMs) {
    parser_.setQuality(config_.quality);
    applyProbe(nowMs);
}

void GpsSource::setPower(bool on, uint32_t nowMs) {
    if (on == powered_) return;

    powered_ = on;
    M5.Power.setExtOutput(on);

    if (on) {
        // Modul potrzebuje chwili od podania napiecia do pierwszego zdania.
        // Odliczanie proby rusza od tego momentu, zeby cisza przy rozruchu
        // nie zjadla jednego z czterech podejsc.
        probeStartedMs_ = nowMs;
        if (!locked_) parser_.reset();
    }
}

bool GpsSource::update(uint32_t nowMs) {
    if (!powered_) return false;

    bool newSample = false;

    while (serial_.available() > 0) {
        const char c = static_cast<char>(serial_.read());
        ++probeBytes_;

        if (echo_ != nullptr) {
            // Przy zle dobranej predkosci transmisji leca bajty spoza ASCII —
            // wypisane wprost potrafia rozsypac terminal, wiec zastepujemy je
            // kropka. Sam fakt, ze cokolwiek leci, jest juz informacja.
            const bool printable = (c >= 32 && c < 127) || c == '\r' || c == '\n';
            echo_->write(printable ? c : '.');
        }

        const gps::Sentence sentence = parser_.feed(c);
        if (sentence == gps::Sentence::None) continue;

        // Zdanie z poprawna suma kontrolna = ustawienia UART-u sa wlasciwe.
        lastSentenceMs_ = nowMs;
        locked_ = true;

        if (sentence != gps::Sentence::Rmc) continue;

        newSample = true;
        if (parser_.fix().valid) {
            lastFixMs_ = nowMs;
            everFixed_ = true;
        }
    }

    // Cisza po udanym dobraniu ustawien oznacza, ze modul zostal odlaczony
    // albo sie zawiesil — wracamy do prob zamiast czekac w nieskonczonosc.
    if (locked_ && nowMs - lastSentenceMs_ > config_.probeMs * 5) {
        locked_ = false;
        probeStartedMs_ = nowMs;
    }

    if (!locked_) advanceProbe(nowMs);

    return newSample;
}

motion::SpeedSample GpsSource::speed(uint32_t nowMs) const {
    motion::SpeedSample sample;
    const gps::NmeaFix& fix = parser_.fix();

    // Dwa warunki, nie jeden: fix ma byc potwierdzony ORAZ swiezy. Stary odczyt
    // podany jako biezacy zafalszowalby korekcje przechylu w zakrecie.
    sample.valid = fix.valid && hasFix(nowMs);
    sample.kmh = sample.valid ? fix.speedKmh : 0.0f;
    return sample;
}

bool GpsSource::hasFix(uint32_t nowMs) const {
    if (!powered_ || !everFixed_) return false;
    return nowMs - lastFixMs_ <= config_.fixMaxAgeMs;
}

bool GpsSource::takeProbeReport(GpsProbeReport& out) {
    if (!reportPending_) return false;
    out = report_;
    reportPending_ = false;
    return true;
}

void GpsSource::applyProbe(uint32_t nowMs) {
    // Kombinacje: obie kolejnosci pinow razy kazda predkosc transmisji.
    // Kolejnosc nie jest przypadkowa — oba piny przy danej predkosci ida obok
    // siebie, bo predkosc jest tu grubsza niewiadoma niz kolejnosc zyl.
    const bool swapped = (probeIndex_ % 2) == 1;

    rxPin_ = swapped ? config_.txPin : config_.rxPin;
    const int txPin = swapped ? config_.rxPin : config_.txPin;
    baud_ = kBauds[probeIndex_ / 2];

    if (started_) serial_.end();
    serial_.begin(baud_, SERIAL_8N1, rxPin_, txPin);
    started_ = true;

    // Liczniki zdan sa dowodem w tej probie, wiec nie moga niesc smieci
    // z poprzedniej.
    parser_.reset();
    probeStartedMs_ = nowMs;
    probeBytes_ = 0;
}

void GpsSource::advanceProbe(uint32_t nowMs) {
    if (nowMs - probeStartedMs_ < config_.probeMs) return;

    report_.baud = baud_;
    report_.rxPin = rxPin_;
    report_.bytes = probeBytes_;
    report_.validSentences = parser_.validSentences();
    report_.rejectedSentences = parser_.rejectedSentences();
    reportPending_ = true;

    probeIndex_ = static_cast<uint8_t>((probeIndex_ + 1) % kProbeCount);
    applyProbe(nowMs);
}

}  // namespace hal
