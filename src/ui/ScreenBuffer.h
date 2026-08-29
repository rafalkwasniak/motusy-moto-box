// Motusy Moto Box — wspolny bufor rysowania dla wszystkich widokow.
//
// Rysowanie prosto na ST7789 migocze przy pelnym odswiezeniu. Sprite 240x135
// w 16 bitach to ~65 kB i trzymamy go w pamieci WEWNETRZNEJ: firmware zajmuje
// raptem 25 z 320 kB, wiec miejsca jest pod dostatkiem, a PSRAM pobieralby
// prad takze w czuwaniu (light sleep) — przy baterii 250 mAh to sie liczy.
//
// Gdy alokacja sie nie powiedzie, rysujemy bezposrednio na ekran: gorzej
// wyglada, ale urzadzenie dziala.

#pragma once

#include <M5Unified.h>

namespace ui {

class ScreenBuffer {
public:
    void begin();

    /// Cel rysowania — sprite albo, awaryjnie, sam wyswietlacz.
    m5gfx::LovyanGFX* gfx();

    /// Przenosi zawartosc bufora na ekran. Bez bufora nie robi nic.
    void present();

    bool isBuffered() const { return buffered_; }

private:
    M5Canvas canvas_{&M5.Display};
    bool buffered_ = false;
};

}  // namespace ui
