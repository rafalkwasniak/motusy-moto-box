// Motusy Moto Box — ekran startowy (§5 specyfikacji).
//
// Specyfikacja przewiduje 5 sekund logo. Zamiast martwego czasu wykorzystujemy
// ten moment jako okno diagnostyczne: urzadzenie nie ma innego interfejsu, wiec
// informacja "IMU OK / KALIBRACJA BRAK" przy starcie jest jedyna okazja, zeby
// uzytkownik dowiedzial sie o problemie.

#pragma once

#include <M5Unified.h>

#include <cstdint>

namespace ui {

class SplashScreen {
public:
    /// Czarne tlo + logo. Wywolac raz, na poczatku sekwencji startowej.
    void begin();

    /// Jedna linia statusu pod logo. `ok == false` wyswietla ja na czerwono.
    void setStatus(const char* text, bool ok = true);

    /// Pasek postepu odliczajacy 5 sekund ekranu startowego. 0.0 .. 1.0
    void setProgress(float fraction);

private:
    void clearStatusArea();

    // Logo ma 168x72 i jest wysrodkowane, wiec po bokach zostaje po 36 px.
    // Ponizej: linia statusu malym fontem i pasek postepu — z duzym zapasem
    // od dolnej krawedzi, zeby logo "oddychalo".
    static constexpr int kLogoY = 12;
    static constexpr int kLogoBottom = 84;
    static constexpr int kStatusY = 100;
    static constexpr int kProgressY = 116;
    static constexpr int kProgressHeight = 4;
    static constexpr int kProgressMargin = 36;
};

}  // namespace ui
