// Motusy Moto Box — wazenie czasowe Fast (IEC 61672-1).
//
// Miernik nie pokazuje chwilowego cisnienia, tylko jego wygladzona srednia.
// Stala czasowa Fast (tau = 125 ms) jest ta sama, co w pierwszym urzadzeniu —
// zostajemy przy niej mimo ze dla "halasu utrzymanego" Slow wydaje sie
// naturalniejszy. Dwa powody: spojnosc z DBMeterV1 oraz to, ze wygladzanie
// i tak zrobi za nas erozja w SustainedLevel. Usredniac dwa razy to gubic
// informacje bez zysku.
//
//   msq[n] = msq[n-1] + alfa * (x[n]^2 - msq[n-1])     alfa = 1 - exp(-T/tau)
//   L[n]   = 10 * log10(msq[n])
//
//   Fast: tau = 0,125 s  ->  alfa = 1,667e-4 przy 48 kHz
//
// USREDNIAMY MOC, NIE DECYBELE. Dla sygnalu skaczacego miedzy 40 a 80 dB
// poprawnie jest 10*log10((10^4 + 10^8)/2) = 77,0 dB, a nie (40+80)/2 = 60,0.
// Siedemnascie decybeli roznicy. Motocykl na przepustnicy to dokladnie taki
// sygnal, wiec to nie jest subtelnosc.
//
// TEMPO OPADANIA, ktore trzeba znac czytajac wyniki: po ustaniu dzwieku
// poziom spada 10*log10(e)/tau = 34,7 dB na sekunde. Pik 70 dB ponad tlem
// wraca do tla dopiero po dwoch sekundach — i to jest wlasciwosc normy,
// nie usterka. Ma to znaczenie przy seriach pikow (patrz test_noise).
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

namespace noise {

class TimeWeighting {
public:
    /// Ponizej tego poziomu nie schodzimy — logarytm z zera nie istnieje,
    /// a cisza cyfrowa i tak nie niesie informacji.
    static constexpr float kMinDb = -140.0f;

    explicit TimeWeighting(float tauS = 0.125f, float sampleRateHz = 48000.0f) {
        configure(tauS, sampleRateHz);
    }

    void configure(float tauS, float sampleRateHz);

    void reset() { meanSquare_ = 0.0f; }

    /// Jedna probka po wazeniu A. Zwraca biezaca srednia moc.
    float process(float x) {
        meanSquare_ += alpha_ * (x * x - meanSquare_);
        return meanSquare_;
    }

    float meanSquare() const { return meanSquare_; }

    /// Moc na decybele wzgledem pelnej skali (x w <-1, 1> daje wynik <= 0).
    static float toDb(float meanSquare);

private:
    float alpha_ = 0.0f;
    float meanSquare_ = 0.0f;
};

}  // namespace noise
