// Motusy Moto Box — ekran INTEGRACJA (KEY2 przez 6 s).
//
// Pokazuje, czy urzadzenie ma komplet potrzebny do wysylania wynikow na
// motobox.motusy.top: siec domowa i token konta. Wchodzi sie tu raz, przy
// uruchamianiu urzadzenia — potem juz sie o tym ekranie zapomina.
//
// Token jest pokazany w postaci zakrytej: wlascicielowi wystarczy sprawdzic,
// czy to ten token, a przypadkowy widz przy motocyklu nie ma go jak odczytac.

#pragma once

#include <cstdint>

#include "ScreenBuffer.h"

namespace ui {

struct IntegrationViewModel {
    /// Nazwa zapisanej sieci; pusty napis gdy nie ustawiono.
    const char* ssid = "";
    /// Token w postaci zakrytej.
    const char* tokenMask = "";
    bool hasNetwork = false;
    bool hasToken = false;
    /// Czy jest komplet: siec i token.
    bool configured = false;
    /// Ile przejazdow czeka na wyslanie — pokazuje, co ta konfiguracja odblokuje.
    uint32_t pendingUploads = 0;
};

class IntegrationView {
public:
    void draw(ScreenBuffer& buffer, const IntegrationViewModel& model);

private:
    static constexpr int kTitleY = 16;
    static constexpr int kNetworkY = 46;
    static constexpr int kTokenY = 66;
    static constexpr int kStatusY = 92;
    static constexpr int kHintY = 122;
    /// Odstep etykiety od wartosci — etykiety sa krotkie i stalej dlugosci.
    static constexpr int kValueX = 66;
};

}  // namespace ui
