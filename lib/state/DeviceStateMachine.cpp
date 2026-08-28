#include "DeviceStateMachine.h"

namespace state {

void DeviceStateMachine::begin(bool externalPower, uint32_t nowMs) {
    confirmedPower_ = externalPower;
    pendingPower_ = externalPower;
    pendingSinceMs_ = nowMs;
    powerLostAtMs_ = nowMs;
    state_ = externalPower ? DeviceState::Riding : DeviceState::Cooldown;
}

bool DeviceStateMachine::debouncePower(bool externalPower, uint32_t nowMs) {
    if (externalPower != pendingPower_) {
        pendingPower_ = externalPower;
        pendingSinceMs_ = nowMs;
        return confirmedPower_;
    }

    if (pendingPower_ == confirmedPower_) return confirmedPower_;

    // Zanik potwierdzamy dluzej niz powrot — patrz komentarz w naglowku.
    const uint32_t required =
        pendingPower_ ? config_.powerReturnConfirmMs : config_.powerLossConfirmMs;

    if (nowMs - pendingSinceMs_ >= required) {
        confirmedPower_ = pendingPower_;
    }
    return confirmedPower_;
}

DeviceEvent DeviceStateMachine::update(bool externalPower, bool alarmEnabled,
                                       bool motionDetected, uint32_t nowMs) {
    const bool hadPower = confirmedPower_;
    const bool power = debouncePower(externalPower, nowMs);

    // Powrot zasilania ma pierwszenstwo nad wszystkim: stacyjka wlaczona
    // oznacza rozbrojenie alarmu i nowa sesje, niezaleznie od stanu (§21).
    if (power && !hadPower) {
        state_ = DeviceState::Riding;
        return DeviceEvent::RideStarted;
    }

    if (!power && hadPower) {
        state_ = DeviceState::Cooldown;
        powerLostAtMs_ = nowMs;
        return DeviceEvent::PowerLost;
    }

    switch (state_) {
        case DeviceState::Riding:
            break;

        case DeviceState::Cooldown:
            if (nowMs - powerLostAtMs_ >= config_.armingDelayMs) {
                state_ = alarmEnabled ? DeviceState::Armed : DeviceState::Idle;
                return DeviceEvent::ScreenOff;
            }
            break;

        case DeviceState::Idle:
            // Alarm mozna wlaczyc takze po zgaszeniu ekranu.
            if (alarmEnabled) state_ = DeviceState::Armed;
            break;

        case DeviceState::Armed:
            if (!alarmEnabled) {
                state_ = DeviceState::Idle;
                break;
            }
            if (motionDetected) {
                state_ = DeviceState::Triggered;
                return DeviceEvent::MotionDetected;
            }
            break;

        case DeviceState::Triggered:
            if (!alarmEnabled) {
                state_ = DeviceState::Idle;
                return DeviceEvent::AlarmCleared;
            }
            break;
    }

    return DeviceEvent::None;
}

uint32_t DeviceStateMachine::msUntilScreenOff(uint32_t nowMs) const {
    if (state_ != DeviceState::Cooldown) return 0;
    const uint32_t elapsed = nowMs - powerLostAtMs_;
    if (elapsed >= config_.armingDelayMs) return 0;
    return config_.armingDelayMs - elapsed;
}

void DeviceStateMachine::silence(uint32_t nowMs) {
    if (state_ != DeviceState::Triggered) return;
    state_ = DeviceState::Idle;
    powerLostAtMs_ = nowMs;
}

void DeviceStateMachine::rearm() {
    if (state_ != DeviceState::Triggered) return;
    state_ = DeviceState::Armed;
}

const char* stateName(DeviceState state) {
    switch (state) {
        case DeviceState::Riding: return "LOTKA";
        case DeviceState::Cooldown: return "CZUWANIE";
        case DeviceState::Idle: return "USPIENIE";
        case DeviceState::Armed: return "ALARM";
        case DeviceState::Triggered: return "RUCH!";
    }
    return "?";
}

}  // namespace state
