// Motusy Moto Box — podglad akcji przycisku w trakcie trzymania.
//
// Rozwiazuje praktyczny problem §22: trzy akcje na jednym przycisku, rozrozniane
// czasem, ktorego uzytkownik nie ma jak odmierzyc — zwlaszcza w rekawicy.
//
// Ekran pokazuje, co sie stanie PO PUSZCZENIU w tej chwili, oraz ile brakuje
// do nastepnego progu. Dzieki temu decyzja jest widoczna zanim zostanie podjeta,
// a pomylka polega najwyzej na potrzymaniu dluzej.

#pragma once

#include <M5Unified.h>

#include "ButtonFsm.h"
#include "ScreenBuffer.h"

namespace ui {

class HoldPrompt {
public:
    void draw(ScreenBuffer& buffer, const input::ButtonFsm& fsm);

private:
    static uint16_t accentFor(input::ButtonAction action);

    static constexpr int kHintY = 24;
    static constexpr int kActionY = 60;
    static constexpr int kBarY = 88;
    static constexpr int kBarHeight = 7;
    static constexpr int kBarMargin = 24;
    static constexpr int kNextY = 112;
};

}  // namespace ui
