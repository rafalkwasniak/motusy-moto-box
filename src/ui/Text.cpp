#include "Text.h"

namespace ui {
namespace text {
namespace {

/// Od najwiekszego do najmniejszego. Pierwszy, ktory sie miesci, wygrywa.
struct FontStep {
    const lgfx::IFont* font;
    int height;
};

const FontStep kSteps[] = {
    {&fonts::FreeSansBold12pt7b, 24},
    {&fonts::FreeSansBold9pt7b, 18},
    {&fonts::Font2, 16},
    {&fonts::Font0, 8},
};

constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

size_t pickStep(m5gfx::LovyanGFX* gfx, const char* str, int maxWidth) {
    for (size_t i = 0; i < kStepCount; ++i) {
        gfx->setFont(kSteps[i].font);
        if (gfx->textWidth(str) <= maxWidth) return i;
    }
    return kStepCount - 1;
}

}  // namespace

int drawFitted(m5gfx::LovyanGFX* gfx, const char* str, int x, int y, int maxWidth) {
    const size_t step = pickStep(gfx, str, maxWidth);
    gfx->setFont(kSteps[step].font);
    gfx->drawString(str, x, y);
    return kSteps[step].height;
}

int fittedHeight(m5gfx::LovyanGFX* gfx, const char* str, int maxWidth) {
    return kSteps[pickStep(gfx, str, maxWidth)].height;
}

}  // namespace text
}  // namespace ui
