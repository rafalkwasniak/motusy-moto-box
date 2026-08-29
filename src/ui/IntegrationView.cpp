#include "IntegrationView.h"

#include <cstdio>

#include "Text.h"
#include "Theme.h"

namespace ui {

void IntegrationView::draw(ScreenBuffer& buffer, const IntegrationViewModel& model) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();
    gfx->fillScreen(color::kBackground);

    gfx->setTextDatum(middle_center);
    gfx->setTextColor(color::kCalibration);
    text::drawFitted(gfx, "INTEGRACJA", layout::kScreenWidth / 2, kTitleY, layout::kContentWidth);

    gfx->drawFastHLine(0, 30, layout::kScreenWidth, color::kDivider);

    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_left);

    gfx->setTextColor(color::kMuted);
    gfx->drawString("SIEC", layout::kContentLeft, kNetworkY);
    gfx->drawString("TOKEN", layout::kContentLeft, kTokenY);

    // Brak ustawienia swieci na szaro jak wartosc zerowa — to nie usterka,
    // tylko rzecz jeszcze niezrobiona.
    gfx->setTextColor(model.hasNetwork ? color::kPrimary : color::kZero);
    gfx->drawString(model.hasNetwork ? model.ssid : "NIE USTAWIONO", kValueX, kNetworkY);

    gfx->setTextColor(model.hasToken ? color::kPrimary : color::kZero);
    gfx->drawString(model.hasToken ? model.tokenMask : "NIE USTAWIONO", kValueX, kTokenY);

    char status[48];
    if (model.configured) {
        std::snprintf(status, sizeof(status), "GOTOWE - DO WYSLANIA: %u",
                      static_cast<unsigned>(model.pendingUploads));
    } else {
        std::snprintf(status, sizeof(status), "BRAK KONFIGURACJI");
    }
    gfx->setTextDatum(middle_center);
    gfx->setTextColor(model.configured ? color::kRiding : color::kWaiting);
    gfx->drawString(status, layout::kScreenWidth / 2, kStatusY);

    gfx->setTextColor(color::kMuted);
    gfx->drawString("KLIK = POWROT", layout::kScreenWidth / 2, kHintY);

    buffer.present();
}

}  // namespace ui
