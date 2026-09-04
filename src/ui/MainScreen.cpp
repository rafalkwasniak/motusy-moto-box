#include "MainScreen.h"

#include <cstdio>
#include <cstring>

#include "Rounding.h"

namespace ui {
namespace {

/// Jak sformatowac wartosc i jaka jednostke dopisac.
enum class RowKind { Degrees, Force, Speed };

struct RowSpec {
    const char* label;
    RowKind kind;
    uint16_t accent;
    /// Wskaznik na pole w RideValues — dzieki temu wiersze sa jedna tabela,
    /// a nie czterema rownoleglymi listami, ktore latwo rozjechac.
    float motion::RideValues::*field;
};

const RowSpec kRows[layout::kRowCount] = {
    {"LEWE",  RowKind::Degrees, color::kLean,  &motion::RideValues::maxLeanLeftDeg},
    {"PRAWE", RowKind::Degrees, color::kLean,  &motion::RideValues::maxLeanRightDeg},
    {"WIOOO", RowKind::Force,   color::kAccel, &motion::RideValues::maxAccelG},
    {"PRRRR", RowKind::Force,   color::kBrake, &motion::RideValues::maxBrakeG},
    // ZIUM, nie MAX: "MAX" jest juz naglowkiem lewej kolumny (MAX OGOLNIE),
    // wiec ta sama nazwa opisywalaby dwie rozne rzeczy na jednym ekranie.
    {"ZIUM",  RowKind::Speed,   color::kSpeed, &motion::RideValues::maxSpeedKmh},
};

const char* unitFor(RowKind kind) {
    switch (kind) {
        case RowKind::Force: return "g";
        case RowKind::Speed: return "km/h";
        default: return nullptr;
    }
}

void formatValue(char* out, size_t size, float value, RowKind kind) {
    switch (kind) {
        case RowKind::Degrees:
            // Przechyl w pelnych stopniach: estymacja z zyroskopu ma dokladnosc
            // rzedu 3-5 stopni, wiec czesci dziesietne bylyby fikcja.
            // motion::roundHalfUp, a nie wlasne zaokraglenie — ta sama funkcja
            // liczy wartosc wysylana do API, wiec ekran i strona nie moga
            // pokazac dwoch roznych liczb.
            std::snprintf(out, size, "%d", motion::roundHalfUp(value));
            break;
        case RowKind::Speed:
            // Predkosc z GPS to pomiar bezposredni, znacznie dokladniejszy niz
            // reszta wartosci — ale pelne km/h i tak wystarcza. Podloga na
            // jedynce jest ta sama, co w przesylce do API: pomiar bliski zeru
            // nie moze wygladac jak brak pomiaru.
            std::snprintf(out, size, "%d", motion::roundSpeedKmh(value));
            break;
        case RowKind::Force:
            std::snprintf(out, size, "%.2f", static_cast<double>(value));
            break;
    }
}

int rowCenterY(int index) {
    return layout::kRowTop + index * layout::kRowHeight + layout::kRowHeight / 2;
}

uint16_t batteryColor(int percent) {
    if (percent <= 20) return color::kAlarm;
    if (percent <= 40) return color::kWaiting;
    return color::kMuted;
}

}  // namespace

void MainScreen::draw(ScreenBuffer& buffer, const MainScreenModel& model) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();

    gfx->fillScreen(color::kBackground);
    drawStatusBar(gfx, model);
    drawHeaders(gfx, model);
    drawRows(gfx, model);

    buffer.present();
}

void MainScreen::drawStatusBar(m5gfx::LovyanGFX* gfx, const MainScreenModel& model) {
    gfx->setFont(&fonts::Font2);
    gfx->setTextDatum(middle_left);
    gfx->setTextColor(model.stateColor);
    gfx->drawString(model.stateLabel, layout::kLabelX, layout::kStatusBarHeight / 2);

    // Odznaki modulow, dosuniete do rezerwy na wskaznik zasilania. GPX lezy
    // blizej krawedzi, tuz obok EXT.
    const int gpxX = layout::kRideRightX - layout::kPowerReserveW - layout::kBadgeW;
    const int almX = gpxX - layout::kBadgeGap - layout::kBadgeW;
    drawBadge(gfx, almX, "ALM", model.alarmEnabled, color::kAlarm);
    drawBadge(gfx, gpxX, "GPX", model.trackEnabled, color::kSpeed);

    // Zrodlo zasilania: przy zasilaniu z motocykla stan baterii nie jest istotny.
    gfx->setFont(&fonts::Font2);
    gfx->setTextDatum(middle_right);
    if (model.externalPower) {
        gfx->setTextColor(color::kRiding);
        gfx->drawString("EXT", layout::kRideRightX, layout::kStatusBarHeight / 2);
    } else {
        char text[8];
        std::snprintf(text, sizeof(text), "%d%%", model.batteryPercent);
        gfx->setTextColor(batteryColor(model.batteryPercent));
        gfx->drawString(text, layout::kRideRightX, layout::kStatusBarHeight / 2);
    }

    gfx->drawFastHLine(0, layout::kStatusBarHeight, layout::kScreenWidth, color::kDivider);
}

void MainScreen::drawBadge(m5gfx::LovyanGFX* gfx, int x, const char* label, bool enabled,
                           uint16_t accent) {
    // Wypelniona gdy modul wlaczony, sam obrys gdy wylaczony. Rozroznienie musi
    // dzialac bez czytania napisu — na ekranie 240x135 odznaka jest widoczna
    // katem oka, a jej tresc juz nie.
    const int y = (layout::kStatusBarHeight - layout::kBadgeH) / 2;

    if (enabled) {
        gfx->fillRoundRect(x, y, layout::kBadgeW, layout::kBadgeH, 3, accent);
        gfx->setTextColor(color::kBackground);
    } else {
        gfx->drawRoundRect(x, y, layout::kBadgeW, layout::kBadgeH, 3, color::kDivider);
        gfx->setTextColor(color::kDivider);
    }

    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_center);
    gfx->drawString(label, x + layout::kBadgeW / 2, layout::kStatusBarHeight / 2);
}

void MainScreen::drawHeaders(m5gfx::LovyanGFX* gfx, const MainScreenModel& model) {
    gfx->setFont(&fonts::Font0);
    gfx->setTextDatum(middle_center);
    gfx->setTextColor(color::kMuted);

    const int centerY = layout::kHeaderY + layout::kHeaderHeight / 2;
    gfx->drawString(model.leftHeader,
                    (layout::kLabelDividerX + layout::kColumnDividerX) / 2, centerY);
    gfx->drawString(model.rightHeader, (layout::kColumnDividerX + layout::kRideRightX) / 2,
                    centerY);

    // Dwie pionowe linie: etykiety | wartosci | wartosci.
    const int lineTop = layout::kHeaderY;
    const int lineHeight = layout::kScreenHeight - layout::kHeaderY;
    gfx->drawFastVLine(layout::kLabelDividerX, lineTop, lineHeight, color::kDivider);
    gfx->drawFastVLine(layout::kColumnDividerX, lineTop, lineHeight, color::kDivider);
}

void MainScreen::drawRows(m5gfx::LovyanGFX* gfx, const MainScreenModel& model) {
    for (int i = 0; i < layout::kRowCount; ++i) {
        const RowSpec& row = kRows[i];
        const int centerY = rowCenterY(i);

        gfx->setFont(&fonts::Font2);
        gfx->setTextDatum(middle_left);
        gfx->setTextColor(color::kMuted);
        gfx->drawString(row.label, layout::kLabelX, centerY);

        const bool speedRow = row.kind == RowKind::Speed;

        char text[12];

        // Zero w predkosci znaczy "nie bylo czym zmierzyc" (motion::Rounding.h:
        // podloga wyniku to 1 km/h), wiec idzie jako kreski. Pozostale cztery
        // wartosci pokazuja zero jako zero.
        //
        // O pustce decyduje WYLACZNIE zapisany rekord, nigdy biezacy stan GPS-a.
        // Bramka na hasFix() kasowala tu poprawne wyniki: zasilanie modulu jest
        // odcinane poza jazda (§2.5), wiec po zgaszeniu stacyjki — i na kazdej
        // stronie historii — zmierzone km/h zamienialy sie w kreski. Brak fixa
        // i tak zostawia rekord na zerze, wiec osobny warunek niczego nie wnosil.
        const bool leftEmpty = speedRow && model.overall.*(row.field) <= 0.0f;
        const bool rightEmpty = speedRow && model.ride.*(row.field) <= 0.0f;

        if (!model.leftPresent || leftEmpty) {
            drawValue(gfx, layout::kOverallRightX, centerY, "---", nullptr, false, color::kZero);
        } else {
            const float value = model.overall.*(row.field);
            formatValue(text, sizeof(text), value, row.kind);
            drawValue(gfx, layout::kOverallRightX, centerY, text, unitFor(row.kind),
                      row.kind == RowKind::Degrees,
                      value > 0.0f ? row.accent : color::kZero);
        }

        if (!model.rightPresent || rightEmpty) {
            drawValue(gfx, layout::kRideRightX, centerY, "---", nullptr, false, color::kZero);
        } else {
            const float value = model.ride.*(row.field);
            formatValue(text, sizeof(text), value, row.kind);
            drawValue(gfx, layout::kRideRightX, centerY, text, unitFor(row.kind),
                      row.kind == RowKind::Degrees,
                      value > 0.0f ? row.accent : color::kZero);
        }
    }
}

void MainScreen::drawValue(m5gfx::LovyanGFX* gfx, int rightX, int centerY, const char* text,
                           const char* unit, bool degreeMark, uint16_t textColor) {
    // Font0 ma stala szerokosc 6 px na znak — wystarczy do rezerwacji miejsca.
    int unitWidth = 0;
    if (degreeMark) {
        unitWidth = 9;
    } else if (unit != nullptr) {
        unitWidth = static_cast<int>(std::strlen(unit)) * 6 + 3;
    }

    const int numberRightX = rightX - unitWidth;

    gfx->setFont(&fonts::FreeSansBold9pt7b);
    gfx->setTextDatum(middle_right);
    gfx->setTextColor(textColor);
    gfx->drawString(text, numberRightX, centerY);

    if (degreeMark) {
        // Znak stopnia rysowany jako pierscien — wbudowane fonty nie maja
        // pewnego pokrycia dla znakow spoza ASCII.
        gfx->drawCircle(numberRightX + 5, centerY - 5, 2, textColor);
    } else if (unit != nullptr) {
        gfx->setFont(&fonts::Font0);
        gfx->setTextDatum(middle_left);
        gfx->setTextColor(textColor == color::kZero ? color::kZero : color::kMuted);
        gfx->drawString(unit, numberRightX + 3, centerY);
    }
}

}  // namespace ui
