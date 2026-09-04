// Motusy Moto Box — rozpoznawanie akcji przycisku po czasie przytrzymania.
//
// Realizuje §22 i §23 specyfikacji funkcjonalnej.
//
// DRABINKA:
//
//     klik      ->  przelaczenie modulu alarmowego
//     2 - 4 s   ->  slad trasy GPX
//     4 - 6 s   ->  reset wynikow
//     6 - 8 s   ->  kalibracja montazu
//     od 8 s    ->  integracja ze strona (WiFi i token)
//
// KOLEJNOSC WYNIKA Z CZESTOSCI UZYCIA, nie z wagi akcji (decyzja uzytkownika,
// 2026-09-04). Alarm i slad to dwa przelaczniki uzywane regularnie, wiec leza
// najplycej. Reset wynikow jest sporadyczny — zeruje takze rekord predkosci.
// Kalibracje i integracje robi sie praktycznie raz w zyciu urzadzenia i osiem
// sekund przytrzymania jest tam kosztem przyjetym swiadomie.
//
// SEDNO §23: akcja jest rozpoznawana DOPIERO NA PUSZCZENIU przycisku, wedlug
// najdluzszego osiagnietego progu. Gdyby akcje odpalaly sie w trakcie trzymania,
// droga do integracji prowadzilaby przez slad, reset i kalibracje — czyli kazde
// wejscie w konfiguracje kasowaloby rekordy i psulo kalibracje po drodze.
//
// DRABINKA JAKO TABELA, a nie jako osobne pola na kazdy prog. Przy czterech
// progach ta sama wiedza byla powtorzona w czterech miejscach (klasyfikacja,
// czas do nastepnego progu, nastepna akcja, pasek postepu na ekranie) i doszycie
// piatego szczebla znaczylo cztery zgodne poprawki. Jedna tabela pilnuje, zeby
// sie nie rozjechaly — tak samo jak wiersze ekranu wynikow w ui/MainScreen.
//
// Dodatkowo `pendingAction()` pozwala pokazac na ekranie, co sie stanie po
// puszczeniu — uzytkownik widzi konsekwencje, zanim je wywola.
//
// Czyste C++ bez zaleznosci od sprzetu: kompiluje sie w srodowisku native.

#pragma once

#include <cstddef>
#include <cstdint>

namespace input {

enum class ButtonAction {
    None,
    /// Klik — przelaczenie modulu alarmowego (§22.1).
    Alarm,
    /// Zapis sladu trasy GPX. Domyslnie wylaczony, wiec przelacznik jest
    /// jedyna droga do wlaczenia go bez telefonu.
    Track,
    /// Reset wynikow (§22.2).
    Reset,
    /// Kalibracja montazu (§22.3).
    Calibration,
    /// Konfiguracja integracji ze strona.
    Integration,
};

/// Jeden szczebel drabinki.
struct ButtonRung {
    /// Od tylu milisekund trzymania obowiazuje ta akcja. Pierwszy szczebel ma
    /// zero — o odrzuceniu drgania styku decyduje osobno `debounceMs`.
    uint32_t fromMs;
    ButtonAction action;
};

/// Ile najwyzej szczebli miesci drabinka. Piec jest w uzyciu; zapas jest po to,
/// zeby dolozenie szostego nie wymagalo ruszania tej stalej.
constexpr size_t kMaxRungs = 8;

struct ButtonFsmConfig {
    /// Nacisniecia krotsze niz to sa odrzucane jako drgania styku albo
    /// przypadkowe muskniecie w rekawicy.
    uint32_t debounceMs = 40;

    /// Progi MUSZA byc uporzadkowane rosnaco, a pierwszy musi wynosic zero.
    /// Wartosci uzywane na urzadzeniu pochodza z src/config.h — te sa domyslne
    /// dla testow i dla srodowiska native.
    size_t rungCount = 5;
    ButtonRung rungs[kMaxRungs] = {
        {0, ButtonAction::Alarm},
        {2000, ButtonAction::Track},
        {4000, ButtonAction::Reset},
        {6000, ButtonAction::Calibration},
        {8000, ButtonAction::Integration},
    };
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

    /// Poczatek biezacego szczebla i poczatek nastepnego [ms od nacisniecia].
    /// Ekran wyboru akcji rysuje z tego pasek postepu W OBREBIE szczebla —
    /// bez tych dwoch liczb musialby powtorzyc cala drabinke u siebie.
    /// Na ostatnim szczeblu obie wartosci sa rowne.
    uint32_t rungStartMs() const;
    uint32_t nextRungStartMs() const;

private:
    /// Indeks szczebla obowiazujacego przy zadanym czasie trzymania.
    /// `rungCount` oznacza "ponizej progu drgania styku".
    size_t rungIndex(uint32_t heldMs) const;

    ButtonFsmConfig config_{};
    bool pressed_ = false;
    uint32_t pressStartMs_ = 0;
    uint32_t nowMs_ = 0;
};

/// Krotka nazwa akcji do wyswietlenia na ekranie.
const char* actionLabel(ButtonAction action);

}  // namespace input
