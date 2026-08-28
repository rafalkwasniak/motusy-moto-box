#include "ScreenBuffer.h"

#include "Theme.h"

namespace ui {

void ScreenBuffer::begin() {
    canvas_.setPsram(true);
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
