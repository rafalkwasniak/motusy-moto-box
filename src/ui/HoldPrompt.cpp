#include "HoldPrompt.h"

#include <cstdio>

#include "Text.h"
#include "Theme.h"

namespace ui {

uint16_t HoldPrompt::accentFor(input::ButtonAction action) {
    switch (action) {
        case input::ButtonAction::ShortPress: return color::kAccel;
        // Reset kasuje rekordy bezpowrotnie — kolor ostrzegawczy.
        case input::ButtonAction::MediumHold: return color::kWaiting;
        case input::ButtonAction::LongHold: return color::kCalibration;
        default: return color::kMuted;
    }
}

void HoldPrompt::draw(ScreenBuffer& buffer, const input::ButtonFsm& fsm) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();
    gfx->fillScreen(color::kBackground);

    const input::ButtonAction pending = fsm.pendingAction();
    const input::ButtonAction next = fsm.nextAction();
    const uint16_t accent = accentFor(pending);

    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_center);
    gfx->setTextColor(color::kMuted);
    gfx->drawString(pending == input::ButtonAction::None ? "TRZYMAJ PRZYCISK" : "PUSC ABY WYKONAC",
                    layout::kScreenWidth / 2, kHintY);

    gfx->setTextColor(accent);
    text::drawFitted(gfx, pending == input::ButtonAction::None ? "..." : input::actionLabel(pending),
                     layout::kScreenWidth / 2, kActionY, layout::kContentWidth);

    // Pasek pokazuje postep w obrebie biezacego progu, nie calego przytrzymania —
    // inaczej po 3 sekundach zatrzymywalby sie na 30% i nic by nie mowil.
    const auto& config = fsm.config();
    const uint32_t held = fsm.heldMs();
    uint32_t lower = 0;
    uint32_t upper = config.mediumHoldMs;
    if (pending == input::ButtonAction::MediumHold) {
        lower = config.mediumHoldMs;
        upper = config.longHoldMs;
    } else if (pending == input::ButtonAction::LongHold) {
        lower = config.longHoldMs;
        upper = config.longHoldMs;
    }

    float fraction = 1.0f;
    if (upper > lower) {
        fraction = static_cast<float>(held - lower) / static_cast<float>(upper - lower);
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
    }

    const int width = layout::kScreenWidth - 2 * kBarMargin;
    gfx->drawRect(kBarMargin - 1, kBarY - 1, width + 2, kBarHeight + 2, color::kDivider);
    gfx->fillRect(kBarMargin, kBarY, static_cast<int>(static_cast<float>(width) * fraction),
                  kBarHeight, accent);

    gfx->setFont(&fonts::Font0);
    gfx->setTextColor(color::kMuted);
    if (next != input::ButtonAction::None) {
        char hint[48];
        std::snprintf(hint, sizeof(hint), "ZA %.1f s: %s",
                      static_cast<double>(fsm.msToNextThreshold()) / 1000.0,
                      input::actionLabel(next));
        text::drawFitted(gfx, hint, layout::kScreenWidth / 2, kNextY, layout::kContentWidth);
    } else {
        text::drawFitted(gfx, "OSTATNI PROG", layout::kScreenWidth / 2, kNextY,
                         layout::kContentWidth);
    }

    buffer.present();
}

}  // namespace ui
