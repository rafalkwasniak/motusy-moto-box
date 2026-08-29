#include "ScreenBuffer.h"

#include "Theme.h"

namespace ui {

void ScreenBuffer::begin() {
    // Bufor w pamieci WEWNETRZNEJ, nie w PSRAM: 65 kB miesci sie z ogromnym
    // zapasem, a PSRAM pobieralby prad rowniez podczas light sleep w czuwaniu.
    canvas_.setPsram(false);
    canvas_.setColorDepth(16);
    buffered_ = canvas_.createSprite(layout::kScreenWidth, layout::kScreenHeight) != nullptr;
}

m5gfx::LovyanGFX* ScreenBuffer::gfx() {
    if (buffered_) return &canvas_;
    return &M5.Display;
}

void ScreenBuffer::present() {
    if (buffered_) canvas_.pushSprite(0, 0);
}

}  // namespace ui
