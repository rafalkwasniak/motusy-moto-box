#include "HardwareView.h"

#include <cstdio>

#include "Theme.h"

namespace ui {

void HardwareView::draw(ScreenBuffer& buffer, const HardwareViewModel& model) {
    m5gfx::LovyanGFX* gfx = buffer.gfx();
    gfx->fillScreen(color::kBackground);
    gfx->setFont(&fonts::Font0);

    gfx->setTextDatum(middle_left);
    gfx->setTextColor(color::kMuted);
    gfx->drawString("SPRZET", layout::kContentLeft, kHeaderY);

    char text[48];

    if (model.scan == nullptr) {
        gfx->drawString("BRAK SKANU I2C", layout::kContentLeft, kListTop);
        buffer.present();
        return;
    }

    const hal::I2cScan& scan = *model.scan;

    gfx->setTextDatum(middle_right);
    std::snprintf(text, sizeof(text), "I2C: %u", scan.totalFound());
    gfx->setTextColor(scan.allCriticalPresent() ? color::kRiding : color::kAlarm);
    gfx->drawString(text, layout::kContentRight, kHeaderY);

    gfx->drawFastHLine(0, 16, layout::kScreenWidth, color::kDivider);

    for (size_t i = 0; i < scan.deviceCount(); ++i) {
        const hal::I2cDevice& device = scan.devices()[i];
        const int y = kListTop + static_cast<int>(i) * kListStep;

        gfx->setTextDatum(middle_left);
        gfx->setTextColor(color::kMuted);
        std::snprintf(text, sizeof(text), "0x%02X", device.address);
        gfx->drawString(text, layout::kContentLeft, y);

        gfx->setTextColor(device.present ? color::kPrimary : color::kZero);
        gfx->drawString(device.name, layout::kContentLeft + 38, y);

        // Brak ukladu krytycznego swieci na czerwono; brak opcjonalnego (RTC,
        // alternatywny adres IMU) jest tylko informacja, nie usterka.
        uint16_t statusColor;
        if (device.present) {
            statusColor = color::kRiding;
        } else {
            statusColor = device.critical ? color::kAlarm : color::kZero;
        }
        gfx->setTextColor(statusColor);
        gfx->drawString(device.present ? "OK" : "BRAK", layout::kContentLeft + 114, y);
    }

    gfx->setTextDatum(middle_left);
    gfx->setTextColor(color::kMuted);
    if (scan.unknownCount() > 0) {
        int written = std::snprintf(text, sizeof(text), "NIEZNANE:");
        for (size_t i = 0; i < scan.unknownCount() && written > 0; ++i) {
            written += std::snprintf(text + written, sizeof(text) - static_cast<size_t>(written),
                                     " 0x%02X", scan.unknown()[i]);
        }
        gfx->setTextColor(color::kWaiting);
        gfx->drawString(text, layout::kContentLeft, kUnknownY);
    } else {
        gfx->drawString("BRAK NIEZNANYCH ADRESOW", layout::kContentLeft, kUnknownY);
    }

    // Zasilanie: stan odfiltrowany, surowy impuls ladowania i najdluzsza
    // zmierzona przerwa miedzy impulsami — po niej dobieramy prog w PowerSource.
    gfx->setTextColor(model.externalPower ? color::kRiding : color::kMuted);
    std::snprintf(text, sizeof(text), "%s  %d.%02dV %d%%  imp:%s luka:%lus  %s",
                  model.externalPower ? "EXT" : "BAT",
                  model.batteryMillivolts / 1000, (model.batteryMillivolts % 1000) / 10,
                  model.batteryPercent, model.charging ? "T" : "N",
                  static_cast<unsigned long>(model.maxPulseGapMs / 1000),
                  model.stateName);
    gfx->drawString(text, layout::kContentLeft, kPowerY);

    // Modul vs. faktyczne czuwanie — te dwie wartosci musza sie zgadzac
    // w stanie ALARM; rozjazd jest natychmiast widoczny.
    gfx->setTextColor(model.alarmEnabled ? color::kRiding : color::kMuted);
    std::snprintf(text, sizeof(text), "ALM modul:%s  czuwa:%s",
                  model.alarmEnabled ? "WL" : "WYL", model.alarmArmed ? "TAK" : "nie");
    gfx->drawString(text, layout::kContentLeft, kAlarmY);

    gfx->setTextColor(color::kMuted);
    std::snprintf(text, sizeof(text), "IMU %.0f Hz  RAM %uk  PSRAM %uk%s",
                  static_cast<double>(model.sampleRateHz),
                  static_cast<unsigned>(model.freeHeapBytes / 1024),
                  static_cast<unsigned>(model.freePsramBytes / 1024),
                  model.bufferedDisplay ? "" : "  (bufor: brak)");
    gfx->drawString(text, layout::kContentLeft, kMemoryY);

    buffer.present();
}

}  // namespace ui
