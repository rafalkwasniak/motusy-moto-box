// Motusy Moto Box — czas trwania przejazdu.
//
// CO MIERZYMY: od pierwszego ruchu do ostatniego ruchu. Postoje w srodku
// (swiatla, korek, przerwa na kawe) wliczaja sie do czasu przejazdu — to nadal
// ta sama jazda. Nie wlicza sie natomiast czas przed ruszeniem i po
// zaparkowaniu, czyli rozgrzewanie silnika na postoju albo zapomniana stacyjka.
//
// DLACZEGO NIE PO PROSTU CZAS OD WLACZENIA STACYJKI: bo pokazywalby "przejazd
// 45 minut" dla dziesieciu minut jazdy i pol godziny gadania przy odpalonym
// motocyklu.
//
// Zrodlem sygnalu "w ruchu" jest `OrientationState::stationary`, ktore ma juz
// wlasne progi i wymaga utrzymania warunkow przez pol sekundy — nie trzeba
// tu drugiego zestawu progow.
//
// PRZERWA W ZASILANIU: licznik przezywa restart na baterii (§25) dzieki
// `restore()`, ale czas samego restartu przepada. Zapisany stan moze byc
// starszy o jeden okres autozapisu, wiec po naglym restarcie czas przejazdu
// bywa zanizony o kilkadziesiat sekund. Przy normalnym zakonczeniu przejazdu
// jest dokladny.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstdint>

namespace motion {

class RideClock {
public:
    /// Odtworzenie z pamieci nieulotnej: tyle sekund przejazd juz trwal.
    void restore(uint32_t seconds);

    /// Nowa sesja jazdy — licznik od zera.
    void reset();

    /// Krok licznika. Wolac w rytmie IMU.
    /// @param moving czy urzadzenie jest w ruchu (negacja `stationary`)
    void update(bool moving, uint32_t nowMs);

    uint32_t seconds() const;

    /// Czy w tej sesji zarejestrowano juz jakikolwiek ruch.
    bool started() const { return started_; }

private:
    /// Sekundy odziedziczone po poprzednim wcieleniu tego samego przejazdu.
    uint32_t baseSeconds_ = 0;
    uint32_t firstMoveMs_ = 0;
    uint32_t lastMoveMs_ = 0;
    bool started_ = false;
};

}  // namespace motion
