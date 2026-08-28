#include "SplashScreen.h"

#include "../assets/logo_asset.h"
#include "Text.h"
#include "Theme.h"

namespace ui {

void SplashScreen::begin() {
    auto& display = M5.Display;
    display.fillScreen(color::kBackground);

    const int x = (layout::kScreenWidth - assets::kLogoWidth) / 2;
    display.drawPng(assets::kLogoPng, assets::kLogoPngLength, x, kLogoY);

    // Ramka paska postepu rysowana raz — potem wypelniamy tylko jej wnetrze.
    const int width = layout::kScreenWidth - 2 * kProgressMargin;
    display.drawRect(kProgressMargin - 1, kProgressY - 1, width + 2, kProgressHeight + 2,
                     color::kDivider);
}

void SplashScreen::clearStatusArea() {
    // Czyscimy tylko pas miedzy logo a paskiem postepu, zeby nie mrugac logiem.
    const int top = kLogoBottom + 1;
    M5.Display.fillRect(0, top, layout::kScreenWidth, kProgressY - top - 2, color::kBackground);
}

void SplashScreen::setStatus(const char* text, bool ok) {
    clearStatusArea();

    // Maly font na stale, nie dobierany do dlugosci: drobna diagnostyka pod logo
    // pasuje do skali urzadzenia, a powiekszona zaczyna z logiem konkurowac.
    auto& display = M5.Display;
    display.setFont(&fonts::Font0);
    display.setTextDatum(middle_center);
    display.setTextColor(ok ? color::kMuted : color::kAlarm, color::kBackground);
    display.drawString(text, layout::kScreenWidth / 2, kStatusY);
}

void SplashScreen::setProgress(float fraction) {
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    const int width = layout::kScreenWidth - 2 * kProgressMargin;
    const int filled = static_cast<int>(width * fraction);

    auto& display = M5.Display;
    display.fillRect(kProgressMargin, kProgressY, filled, kProgressHeight, color::kPrimary);
    display.fillRect(kProgressMargin + filled, kProgressY, width - filled, kProgressHeight,
                     color::kBackground);
}

}  // namespace ui
