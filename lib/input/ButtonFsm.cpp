#include "ButtonFsm.h"

namespace input {

size_t ButtonFsm::rungIndex(uint32_t heldMs) const {
    if (heldMs < config_.debounceMs || config_.rungCount == 0) return config_.rungCount;

    // Od najdluzszego progu — §23 wymaga, zeby dluzsze przytrzymanie wykonalo
    // wylacznie akcje najdluzszego OSIAGNIETEGO szczebla, a nie wszystkie po drodze.
    for (size_t i = config_.rungCount; i > 0; --i) {
        if (heldMs >= config_.rungs[i - 1].fromMs) return i - 1;
    }
    return config_.rungCount;
}

ButtonAction ButtonFsm::update(bool pressed, uint32_t nowMs) {
    nowMs_ = nowMs;

    if (pressed && !pressed_) {
        pressed_ = true;
        pressStartMs_ = nowMs;
        return ButtonAction::None;
    }

    if (!pressed && pressed_) {
        pressed_ = false;
        const size_t index = rungIndex(nowMs - pressStartMs_);
        return index < config_.rungCount ? config_.rungs[index].action : ButtonAction::None;
    }

    return ButtonAction::None;
}

uint32_t ButtonFsm::heldMs() const {
    if (!pressed_) return 0;
    return nowMs_ - pressStartMs_;
}

ButtonAction ButtonFsm::pendingAction() const {
    if (!pressed_) return ButtonAction::None;
    const size_t index = rungIndex(heldMs());
    return index < config_.rungCount ? config_.rungs[index].action : ButtonAction::None;
}

uint32_t ButtonFsm::msToNextThreshold() const {
    if (!pressed_) return 0;
    const uint32_t held = heldMs();
    for (size_t i = 0; i < config_.rungCount; ++i) {
        if (held < config_.rungs[i].fromMs) return config_.rungs[i].fromMs - held;
    }
    return 0;
}

ButtonAction ButtonFsm::nextAction() const {
    if (!pressed_) return ButtonAction::None;
    const uint32_t held = heldMs();
    for (size_t i = 0; i < config_.rungCount; ++i) {
        if (held < config_.rungs[i].fromMs) return config_.rungs[i].action;
    }
    return ButtonAction::None;
}

uint32_t ButtonFsm::rungStartMs() const {
    const size_t index = rungIndex(heldMs());
    if (index >= config_.rungCount) return 0;
    return config_.rungs[index].fromMs;
}

uint32_t ButtonFsm::nextRungStartMs() const {
    const size_t index = rungIndex(heldMs());
    if (index >= config_.rungCount) return config_.rungCount > 0 ? config_.rungs[0].fromMs : 0;
    // Ostatni szczebel: pasek stoi pelny, bo nie ma juz dokad isc.
    if (index + 1 >= config_.rungCount) return config_.rungs[index].fromMs;
    return config_.rungs[index + 1].fromMs;
}

const char* actionLabel(ButtonAction action) {
    switch (action) {
        case ButtonAction::Alarm: return "PRZELACZ ALARM";
        case ButtonAction::Track: return "SLAD GPX";
        case ButtonAction::Reset: return "RESET WYNIKOW";
        case ButtonAction::Calibration: return "KALIBRACJA";
        case ButtonAction::Integration: return "INTEGRACJA";
        case ButtonAction::None: return "";
    }
    return "";
}

}  // namespace input
