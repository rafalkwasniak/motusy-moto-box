#include "SpeedGate.h"

namespace motion {

void SpeedGate::updateSpeed(float kmh, uint32_t nowMs) {
    haveSpeed_ = true;
    lastSpeedMs_ = nowMs;

    if (kmh >= config_.enterKmh) {
        open_ = true;
        below_ = false;
        return;
    }

    if (kmh < config_.exitKmh) {
        // Wybieg liczymy od PIERWSZEJ probki ponizej progu, nie od kazdej
        // kolejnej — inaczej hamowanie do zera odnawialoby go w nieskonczonosc.
        if (!below_) {
            below_ = true;
            belowSinceMs_ = nowMs;
        }
        if (coastExpired(nowMs)) open_ = false;
        return;
    }

    // Miedzy progami: histereza. Jadacy zostaje jadacym, stojacy stojacym,
    // a rozpoczety wybieg przerywamy — motocykl znowu sie toczy.
    below_ = false;
}

bool SpeedGate::isRecording(bool imuStationary, bool gpsResponding, uint32_t nowMs) const {
    if (!hasFreshSpeed(nowMs)) {
        // MODULU NIE MA WCALE. Awaria albo urzadzenie bez GPS-a ma dzialac
        // tak, jak dzialalo przed jego dolozeniem — zostaje regula sprzed GPS.
        if (!gpsResponding) return !imuStationary;

        // Fixu JESZCZE NIE BYLO w tym przejezdzie. Bezruch z IMU nie mowi
        // wtedy nic o tym, czy motocykl jedzie — moze po prostu ktos wzial
        // urzadzenie do reki. Dopoki modul zyje, czekamy na niego.
        if (!haveSpeed_) return false;

        // FIX BYL I ZNIKNAL. Na samym IMU jedziemy dalej TYLKO wtedy, gdy
        // w chwili utraty bramka byla otwarta, czyli motocykl faktycznie
        // jechal powyzej progu.
        //
        // BEZ TEGO WARUNKU DZIURA Z 2026-09-04 WRACALA INNYM WEJSCIEM
        // (zgloszone 2026-09-18). "Mialem fix kiedys w tym przejezdzie" to
        // NIE to samo, co "jechalem, gdy go tracilem": po zlapaniu pozycji
        // na dworze i powrocie pod dach fix juz nie wracal, wiec urzadzenie
        // zostawalo w trybie zapasowym bezterminowo i kazde poruszenie reka
        // ustanawialo rekord przechylu — dokladnie to, co tamta poprawka
        // miala zlikwidowac.
        //
        // Tunel dziala dalej: wjazd przy 80 km/h zostawia `open_` prawda,
        // wiec ostatnia faza hamowania w ciemnosci nadal sie zapisuje.
        if (!open_) return false;

        return !imuStationary;
    }

    if (!open_) return false;
    // Stan liczony takze przy odpytaniu, bo probki przychodza 1 Hz, a petla
    // pyta 100 razy na sekunde — koniec wybiegu ma nastapic co do chwili,
    // a nie dopiero przy nastepnym zdaniu z modulu.
    return !(below_ && coastExpired(nowMs));
}

bool SpeedGate::hasFreshSpeed(uint32_t nowMs) const {
    if (!haveSpeed_) return false;
    return nowMs - lastSpeedMs_ <= config_.fixHoldMs;
}

bool SpeedGate::coastExpired(uint32_t nowMs) const {
    return nowMs - belowSinceMs_ >= config_.coastMs;
}

void SpeedGate::reset() {
    open_ = false;
    below_ = false;
    belowSinceMs_ = 0;
    haveSpeed_ = false;
    lastSpeedMs_ = 0;
}

}  // namespace motion
