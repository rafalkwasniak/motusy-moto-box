// Motusy Moto Box — kalibracja orientacji montazu urzadzenia na motocyklu.
//
// Patrz docs/architektura-techniczna.md §7.
//
// UKLAD MOTOCYKLA (konwencja lotnicza, prawoskretna):
//     X = przod       (kierunek jazdy)
//     Y = prawo
//     Z = dol
//
// W tej konwencji:
//     roll  > 0  ->  przechyl w PRAWO
//     roll  < 0  ->  przechyl w LEWO
//     pitch > 0  ->  przod uniesiony (podjazd / wheelie)
//
// W spoczynku, przy motocyklu ustawionym pionowo, akcelerometr mierzy sile
// wlasciwa (0, 0, -1 g) — skierowana w gore, przeciwnie do osi Z.

#pragma once

#include "Vec3.h"

namespace motion {

/// Ktora os urzadzenia wskazuje przod motocykla. Kalibracja postojowa okresla
/// tylko kierunek pionu (2 z 3 stopni swobody) — trzeci musi pochodzic
/// z konwencji montazu. Patrz docs/architektura-techniczna.md §7.1.
enum class ForwardAxis {
    DeviceXPlus,
    DeviceXMinus,
    DeviceYPlus,
    DeviceYMinus,
    DeviceZPlus,
    DeviceZMinus,
};

Vec3 axisVector(ForwardAxis axis);

/// Przechowuje obrot z ukladu urzadzenia do ukladu motocykla.
/// Domyslnie skonstruowana instancja jest **nieskalibrowana** i dziala jak
/// identycznosc — urzadzenie zachowuje sie tak, jakby bylo zamontowane idealnie.
class MountCalibration {
public:
    MountCalibration() = default;

    /// Wyznacza obrot na podstawie odczytu akcelerometru przy motocyklu stojacym
    /// pionowo i nieruchomo.
    ///
    /// @param restingAccelG  usredniony odczyt akcelerometru [g] w ukladzie urzadzenia
    /// @param forward        ktora os urzadzenia wskazuje przod motocykla
    /// @return false jesli odczyt jest niewiarygodny (modul daleki od 1 g lub
    ///         os "przod" niemal rownolegla do pionu) — kalibracja zostaje bez zmian
    bool calibrateFromRest(const Vec3& restingAccelG, ForwardAxis forward);

    /// Przelicza wektor z ukladu urzadzenia do ukladu motocykla.
    Vec3 toBikeFrame(const Vec3& deviceVector) const { return rotation_ * deviceVector; }

    bool isCalibrated() const { return calibrated_; }

    /// Surowa macierz obrotu — do zapisu w pamieci nieulotnej.
    const Mat3& rotation() const { return rotation_; }

    /// Odtworzenie kalibracji z pamieci nieulotnej.
    void restore(const Mat3& rotation);

    void reset();

    /// Maksymalne odchylenie modulu przyspieszenia od 1 g akceptowane podczas
    /// kalibracji. Wieksze oznacza, ze motocykl nie stal nieruchomo.
    static constexpr float kAccelToleranceG = 0.15f;

    /// Minimalny kat miedzy osia "przod" a pionem. Ponizej tej wartosci
    /// ortogonalizacja jest numerycznie niestabilna — znak zlego wyboru osi.
    static constexpr float kMinForwardTiltRad = degToRad(15.0f);

private:
    Mat3 rotation_{};
    bool calibrated_ = false;
};

}  // namespace motion
