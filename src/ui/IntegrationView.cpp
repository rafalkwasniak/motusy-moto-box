#include "IntegrationView.h"

#include <cstdio>

#include "Text.h"
#include "Theme.h"

namespace ui {

void IntegrationView::draw(ScreenBuffer& buffer, const IntegrationViewModel& model) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();
    gfx->fillScreen(color::kBackground);

    // Tryb portalu dostaje CALY ekran bez naglowka i linii. Te trzydziesci
    // pikseli to roznica miedzy haslem, ktore widac z reki, a takim, ktore
    // trzeba przybliżyc do oczu — a przepisuje sie je w garazu.
    if (model.portalRunning) {
        drawPortal(buffer, model);
        return;
    }

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

namespace {

/// Drabinka fontow WYLACZNIE dla ekranu przepisywania — wlasna, nie ta
/// z text::drawFitted. Tamta zaczyna sie od 12 pt, bo sluzy komunikatom, ktore
/// tylko sie czyta. Tutaj napisy sie PRZEPISUJE, znak po znaku, w garazu,
/// przy kiepskim swietle, patrzac na przemian na ekran i na telefon —
/// wiec zaczynamy od 18 pt i schodzimy nizej tylko wtedy, gdy trzeba.
struct BigFont {
    const lgfx::IFont* font;
    int height;
};

constexpr BigFont kBigSteps[] = {
    {&fonts::FreeSansBold18pt7b, 36},
    {&fonts::FreeSansBold12pt7b, 24},
    {&fonts::FreeSansBold9pt7b, 18},
};

/// Rysuje napis najwiekszym fontem, ktory miesci sie w szerokosci tresci.
void drawBig(m5gfx::LovyanGFX* gfx, const char* text, int y) {
    for (const BigFont& step : kBigSteps) {
        gfx->setFont(step.font);
        if (gfx->textWidth(text) <= layout::kContentWidth) break;
    }
    gfx->drawString(text, layout::kScreenWidth / 2, y);
}

}  // namespace

/// Ekran przepisywania: nazwa sieci i haslo tak duze, jak sie da.
///
/// Uklad jest podporzadkowany jednej czynnosci — przeniesieniu dwoch napisow
/// do telefonu. Dlatego nie ma tu naglowka ani ramek, a adres i stan zeszly
/// do jednej linijki na dole: sa potrzebne raz, a haslo czyta sie znak po znaku.
void IntegrationView::drawPortal(ScreenBuffer& buffer, const IntegrationViewModel& model) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();

    gfx->setTextDatum(middle_center);

    gfx->setFont(&fonts::Font0);
    gfx->setTextColor(color::kMuted);
    gfx->drawString("SIEC", layout::kScreenWidth / 2, kPortalLabel1Y);

    gfx->setTextColor(color::kCalibration);
    drawBig(gfx, model.apSsid, kPortalValue1Y);

    gfx->setFont(&fonts::Font0);
    gfx->setTextColor(color::kMuted);
    gfx->drawString("HASLO", layout::kScreenWidth / 2, kPortalLabel2Y);

    // Haslo na bialo, czyli najmocniejszym kontrastem jaki ekran daje: nazwe
    // sieci wybiera sie z listy w telefonie, ale haslo przepisuje sie znak
    // po znaku i to ono decyduje, czy cala ta operacja sie uda.
    gfx->setTextColor(color::kPrimary);
    drawBig(gfx, model.apPassword, kPortalValue2Y);

    // Adres i stan polaczenia w jednej linii na dole. Adres przydaje sie tylko
    // wtedy, gdy telefon nie otworzy formularza sam; stan mowi, czy juz czas
    // patrzec w telefon, czy jeszcze raz sprawdzic przepisane haslo.
    gfx->setFont(&fonts::Font0);
    if (model.clients > 0) {
        gfx->setTextColor(color::kRiding);
        gfx->drawString("TELEFON POLACZONY", layout::kScreenWidth / 2, kPortalFooterY);
    } else {
        gfx->setTextColor(color::kMuted);
        char footer[40];
        std::snprintf(footer, sizeof(footer), "%s - CZEKAM", model.apAddress);
        gfx->drawString(footer, layout::kScreenWidth / 2, kPortalFooterY);
    }

    buffer.present();
}

}  // namespace ui
