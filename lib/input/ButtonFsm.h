// Motusy Moto Box — rozpoznawanie akcji przycisku po czasie przytrzymania.
//
// Realizuje §22 i §23 specyfikacji funkcjonalnej:
//
//     krotkie nacisniecie  ->  przelaczenie modulu alarmowego
//     okolo 3 sekundy      ->  reset wynikow
//     okolo 10 sekund      ->  kalibracja IMU
//
// SEDNO §23: akcja jest rozpoznawana DOPIERO NA PUSZCZENIU przycisku, wedlug
// najdluzszego osiagnietego progu. Gdyby akcje odpalaly sie w trakcie trzymania,
// droga do kalibracji (10 s) prowadzilaby przez reset wynikow (3 s) — czyli
// uzytkownik chcacy skalibrowac urzadzenie zawsze najpierw kasowalby rekordy.
//
// Dodatkowo `pendingAction()` pozwala pokazac na ekranie, co sie stanie po
// puszczeniu — uzytkownik widzi konsekwencje, zanim ja wywola.
//
// Czyste C++ bez zaleznosci od sprzetu: kompiluje sie w srodowisku native.

#pragma once

#include <cstdint>

namespace input {

enum class ButtonAction {
    None,
    /// Krotkie nacisniecie — §22.1
    ShortPress,
    /// Przytrzymanie ~3 s — §22.2
    MediumHold,
    /// Przytrzymanie ~10 s — §22.3
    LongHold,
};

struct ButtonFsmConfig {
    uint32_t mediumHoldMs = 3000;
    uint32_t longHoldMs = 10000;
    /// Nacisniecia krotsze niz to sa odrzucane jako drgania styku albo
    /// przypadkowe muskniecie w rekawicy.
    uint32_t debounceMs = 40;
};

class ButtonFsm {
public:
    explicit ButtonFsm(const ButtonFsmConfig& config = {}) : config_(config) {}

    const ButtonFsmConfig& config() const { return config_; }

    /// Krok maszyny. Wywolywac w kazdej iteracji petli glownej.
    /// @return akcja rozpoznana w tym kroku — niezerowa tylko w momencie
    ///         puszczenia przycisku.
    ButtonAction update(bool pressed, uint32_t nowMs);

    bool isPressed() const { return pressed_; }

    /// Jak dlugo przycisk jest trzymany [ms]. Zero gdy nie jest.
    uint32_t heldMs() const;

    /// Co sie stanie, jesli uzytkownik puscil przycisk w tej chwili.
    ButtonAction pendingAction() const;

    /// Czas do nastepnego progu [ms]. Zero gdy osiagnieto juz najdluzszy prog.
    uint32_t msToNextThreshold() const;

    /// Akcja przypisana do nastepnego progu, albo None gdy nie ma juz kolejnego.
    ButtonAction nextAction() const;

private:
    ButtonAction classify(uint32_t heldMs) const;

    ButtonFsmConfig config_{};
    bool pressed_ = false;
    uint32_t pressStartMs_ = 0;
    uint32_t nowMs_ = 0;
};

/// Krotka nazwa akcji do wyswietlenia na ekranie.
const char* actionLabel(ButtonAction action);

}  // namespace input
