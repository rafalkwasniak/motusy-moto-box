// Motusy Moto Box — ekran INTEGRACJA (KEY2 przez 6 s).
//
// Pokazuje, czy urzadzenie ma komplet potrzebny do wysylania wynikow na
// motusy.top: siec domowa i token konta. Wchodzi sie tu raz, przy
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

    // ── Punkt dostepowy ────────────────────────────────────────────────────
    /// Gdy portal dziala, ekran pokazuje dane do polaczenia zamiast stanu
    /// konfiguracji: w tej chwili uzytkownik potrzebuje nazwy sieci i hasla,
    /// a nie informacji, ze token jeszcze nie jest ustawiony.
    bool portalRunning = false;
    const char* apSsid = "";
    const char* apPassword = "";
    const char* apAddress = "192.168.4.1";
    /// Ilu klientow jest podlaczonych do punktu dostepowego.
    uint8_t clients = 0;
};

class IntegrationView {
public:
    void draw(ScreenBuffer& buffer, const IntegrationViewModel& model);

private:
    void drawPortal(ScreenBuffer& buffer, const IntegrationViewModel& model);

    static constexpr int kTitleY = 16;
    static constexpr int kNetworkY = 46;
    static constexpr int kTokenY = 66;
    static constexpr int kStatusY = 92;
    static constexpr int kHintY = 122;
    /// Odstep etykiety od wartosci — etykiety sa krotkie i stalej dlugosci.
    static constexpr int kValueX = 66;

    // Ekran przepisywania (portal): dwie wartosci fontem 18 pt, kazda pod
    // wlasna etykieta, i jedna linia stanu na dole. Wysokosci dobrane tak,
    // zeby napis 36-pikselowy nie wchodzil w sasiada.
    static constexpr int kPortalLabel1Y = 10;
    static constexpr int kPortalValue1Y = 36;
    static constexpr int kPortalLabel2Y = 66;
    static constexpr int kPortalValue2Y = 92;
    static constexpr int kPortalFooterY = 126;
};

}  // namespace ui
