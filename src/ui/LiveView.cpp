#include "LiveView.h"

#include <cstdio>

#include "Theme.h"

namespace ui {
namespace {

constexpr int kRollValueY = 34;
constexpr int kBarY = 58;
constexpr int kBarHeight = 9;
constexpr int kBarMargin = 20;
constexpr int kReadoutRow0Y = 82;
constexpr int kReadoutRowStep = 15;
constexpr int kFlagsY = 126;

int barCenterX() { return layout::kScreenWidth / 2; }
int barWidth() { return layout::kScreenWidth - 2 * kBarMargin; }

/// Rysuje etykiete i wartosc jako pare: etykieta przygaszona, wartosc jasna.
void drawPair(m5gfx::LovyanGFX* gfx, int x, int y, const char* label, const char* value,
              uint16_t valueColor) {
    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_left);
    gfx->setTextColor(color::kMuted);
    gfx->drawString(label, x, y);
    gfx->setTextColor(valueColor);
    gfx->drawString(value, x + 40, y);
}

/// Znacznik aktywnosci: wypelniony gdy dana korekcja dziala w tym kroku.
void drawFlag(m5gfx::LovyanGFX* gfx, int x, int y, const char* label, bool active,
              uint16_t activeColor) {
    constexpr int kWidth = 44;
    constexpr int kHeight = 13;
    if (active) {
        gfx->fillRoundRect(x, y - kHeight / 2, kWidth, kHeight, 3, activeColor);
        gfx->setTextColor(color::kBackground);
    } else {
        gfx->drawRoundRect(x, y - kHeight / 2, kWidth, kHeight, 3, color::kDivider);
        gfx->setTextColor(color::kDivider);
    }
    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_center);
    gfx->drawString(label, x + kWidth / 2, y);
}

}  // namespace

void LiveView::draw(ScreenBuffer& buffer, const LiveViewModel& model) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();

    gfx->fillScreen(color::kBackground);
    drawHeader(gfx, model);
    drawLeanIndicator(gfx, model.state.rollDeg());
    drawReadouts(gfx, model);
    drawFlags(gfx, model);

    buffer.present();
}

void LiveView::drawHeader(m5gfx::LovyanGFX* gfx, const LiveViewModel& model) {
    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_left);
    gfx->setTextColor(color::kMuted);
    gfx->drawString("DIAGNOSTYKA", layout::kContentLeft, 8);

    char rate[16];
    std::snprintf(rate, sizeof(rate), "%.0f Hz", static_cast<double>(model.sampleRateHz));
    gfx->setTextDatum(middle_right);
    gfx->setTextColor(model.imuOk ? color::kMuted : color::kAlarm);
    gfx->drawString(model.imuOk ? rate : "IMU BRAK", layout::kContentRight, 8);

    gfx->drawFastHLine(0, 16, layout::kScreenWidth, color::kDivider);

    // Duza wartosc przechylu — glowna rzecz, ktora sprawdzamy przy montazu.
    char roll[16];
    std::snprintf(roll, sizeof(roll), "%+.1f", static_cast<double>(model.state.rollDeg()));
    gfx->setFont(&fonts::FreeSansBold12pt7b);
    gfx->setTextDatum(middle_center);
    gfx->setTextColor(color::kPrimary);
    gfx->drawString(roll, layout::kScreenWidth / 2, kRollValueY);
}

void LiveView::drawLeanIndicator(m5gfx::LovyanGFX* gfx, float rollDeg) {
    const int width = barWidth();
    const int center = barCenterX();

    gfx->drawRect(kBarMargin, kBarY, width, kBarHeight, color::kDivider);

    // Podzialka co 30 stopni. Srodek — pozycja pionowa motocykla.
    for (int tick = -60; tick <= 60; tick += 30) {
        const int x = center + static_cast<int>(static_cast<float>(width) * 0.5f *
                                                (static_cast<float>(tick) / kLeanScaleDeg));
        const uint16_t tone = (tick == 0) ? color::kMuted : color::kDivider;
        gfx->drawFastVLine(x, kBarY, kBarHeight, tone);
    }

    float clamped = rollDeg;
    if (clamped > kLeanScaleDeg) clamped = kLeanScaleDeg;
    if (clamped < -kLeanScaleDeg) clamped = -kLeanScaleDeg;

    const int markerX =
        center + static_cast<int>(static_cast<float>(width) * 0.5f * (clamped / kLeanScaleDeg));

    // Wypelnienie od srodka do znacznika pokazuje kierunek i wielkosc przechylu.
    const int left = (markerX < center) ? markerX : center;
    const int span = (markerX < center) ? (center - markerX) : (markerX - center);
    gfx->fillRect(left, kBarY + 2, span, kBarHeight - 4, color::kAccel);
    gfx->fillRect(markerX - 1, kBarY - 2, 3, kBarHeight + 4, color::kPrimary);
}

void LiveView::drawReadouts(m5gfx::LovyanGFX* gfx, const LiveViewModel& model) {
    char buffer[16];
    const int rightX = layout::kScreenWidth / 2 + 8;

    std::snprintf(buffer, sizeof(buffer), "%+.1f", static_cast<double>(model.state.pitchDeg()));
    drawPair(gfx, layout::kContentLeft, kReadoutRow0Y, "PITCH", buffer, color::kPrimary);

    std::snprintf(buffer, sizeof(buffer), "%+.2f", static_cast<double>(model.state.longitudinalG));
    drawPair(gfx, layout::kContentLeft, kReadoutRow0Y + kReadoutRowStep, "LONG", buffer, color::kAccel);

    std::snprintf(buffer, sizeof(buffer), "%+.2f", static_cast<double>(model.state.lateralG));
    drawPair(gfx, rightX, kReadoutRow0Y, "LAT", buffer, color::kBrake);

    // Offset zyroskopu w stopniach na sekunde — jesli rosnie, cos jest nie tak.
    const float biasDeg = motion::radToDeg(model.state.gyroBiasRadS.norm());
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(biasDeg));
    drawPair(gfx, rightX, kReadoutRow0Y + kReadoutRowStep, "BIAS", buffer, color::kMuted);
}

void LiveView::drawFlags(m5gfx::LovyanGFX* gfx, const LiveViewModel& model) {
    drawFlag(gfx, layout::kContentLeft, kFlagsY, "ACC", model.state.accelCorrectionActive, color::kRiding);
    drawFlag(gfx, layout::kContentLeft + 48, kFlagsY, "GPS", model.state.turnCorrectionActive, color::kAccel);
    drawFlag(gfx, layout::kContentLeft + 96, kFlagsY, "POSTOJ", model.state.stationary, color::kWaiting);
    drawFlag(gfx, layout::kContentLeft + 144, kFlagsY, "KAL", model.mountCalibrated, color::kCalibration);
}

}  // namespace ui
