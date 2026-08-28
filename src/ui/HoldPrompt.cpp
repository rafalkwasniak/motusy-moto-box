#include "HoldPrompt.h"

#include <cstdio>

#include "Theme.h"

namespace ui {
namespace {

/// Wszystkie trzy stany rysuja sie identycznym ukladem i identycznymi
/// czcionkami — rozni je tylko tresc i kolor akcentu. Rozmiar tytulu jest
/// dobrany raz, pod najdluzsza z etykiet, zeby "KALIBRACJA" nie wychodzila
/// wieksza niz "PRZELACZ ALARM".
const lgfx::IFont* titleFont(m5gfx::LovyanGFX* gfx) {
    gfx->setFont(&fonts::FreeSansBold12pt7b);
    if (gfx->textWidth(input::actionLabel(input::ButtonAction::ShortPress)) <=
        layout::kContentWidth) {
        return &fonts::FreeSansBold12pt7b;
    }
    return &fonts::FreeSansBold9pt7b;
}

}  // namespace

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
    const int centerX = layout::kScreenWidth / 2;

    // Linia 1: podpowiedz — zawsze maly font.
    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_center);
    gfx->setTextColor(color::kMuted);
    gfx->drawString(pending == input::ButtonAction::None ? "TRZYMAJ PRZYCISK" : "PUSC ABY WYKONAC",
                    centerX, kHintY);

    // Linia 2: nazwa akcji — jeden wspolny rozmiar dla wszystkich stanow.
    gfx->setFont(titleFont(gfx));
    gfx->setTextColor(accent);
    gfx->drawString(pending == input::ButtonAction::None ? "..." : input::actionLabel(pending),
                    centerX, kActionY);

    // Pasek pokazuje postep w obrebie biezacego progu, nie calego przytrzymania —
    // inaczej po 2 sekundach zatrzymywalby sie i nic by nie mowil.
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

    // Linia 3: co dalej — zawsze maly font, ten sam w kazdym stanie.
    gfx->setFont(&fonts::Font0);
    gfx->setTextColor(color::kMuted);
    if (next != input::ButtonAction::None) {
        char hint[48];
        std::snprintf(hint, sizeof(hint), "ZA %.1f s: %s",
                      static_cast<double>(fsm.msToNextThreshold()) / 1000.0,
                      input::actionLabel(next));
        gfx->drawString(hint, centerX, kNextY);
    } else {
        gfx->drawString("OSTATNI PROG", centerX, kNextY);
    }

    buffer.present();
}

}  // namespace ui
