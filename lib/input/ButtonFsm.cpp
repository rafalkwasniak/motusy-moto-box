#include "ButtonFsm.h"

namespace input {

ButtonAction ButtonFsm::classify(uint32_t heldMs) const {
    // Kolejnosc od najdluzszego progu — §23 wymaga, zeby dluzsze przytrzymanie
    // wykonalo wylacznie akcje najdluzszego osiagnietego progu.
    if (heldMs >= config_.extraHoldMs) return ButtonAction::ExtraHold;
    if (heldMs >= config_.longHoldMs) return ButtonAction::LongHold;
    if (heldMs >= config_.mediumHoldMs) return ButtonAction::MediumHold;
    if (heldMs >= config_.debounceMs) return ButtonAction::ShortPress;
    return ButtonAction::None;
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
        return classify(nowMs - pressStartMs_);
    }

    return ButtonAction::None;
}

uint32_t ButtonFsm::heldMs() const {
    if (!pressed_) return 0;
    return nowMs_ - pressStartMs_;
}

ButtonAction ButtonFsm::pendingAction() const {
    if (!pressed_) return ButtonAction::None;
    return classify(heldMs());
}

uint32_t ButtonFsm::msToNextThreshold() const {
    if (!pressed_) return 0;
    const uint32_t held = heldMs();
    if (held < config_.mediumHoldMs) return config_.mediumHoldMs - held;
    if (held < config_.longHoldMs) return config_.longHoldMs - held;
    if (held < config_.extraHoldMs) return config_.extraHoldMs - held;
    return 0;
}

ButtonAction ButtonFsm::nextAction() const {
    if (!pressed_) return ButtonAction::None;
    const uint32_t held = heldMs();
    if (held < config_.mediumHoldMs) return ButtonAction::MediumHold;
    if (held < config_.longHoldMs) return ButtonAction::LongHold;
    if (held < config_.extraHoldMs) return ButtonAction::ExtraHold;
    return ButtonAction::None;
}

const char* actionLabel(ButtonAction action) {
    switch (action) {
        case ButtonAction::ShortPress: return "PRZELACZ ALARM";
        case ButtonAction::MediumHold: return "RESET WYNIKOW";
        case ButtonAction::LongHold: return "KALIBRACJA";
        case ButtonAction::ExtraHold: return "INTEGRACJA";
        case ButtonAction::None: return "";
    }
    return "";
}

}  // namespace input
