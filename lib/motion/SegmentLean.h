// Motusy Moto Box — najwiekszy przechyl na odcinku sladu trasy.
//
// PO CO ISTNIEJE. Wynik przejazdu i slad pokazywaly rozne przechyly tej samej
// jazdy: wynik mowil "lewo 8 stopni", a slad niosl punkt z -31. Powod byl taki,
// ze kazda sciezka miala WLASNA regule tego, co jest pomiarem:
//
//   wynik przejazdu  ->  100 Hz, ale wylacznie przy otwartej bramce predkosci
//                        (§16) i w oknie 3-60 stopni,
//   slad             ->  odczyt chwilowy raz na sekunde, bez zadnego warunku.
//
// Ponizej 5 km/h bramka jest zamknieta CELOWO — §16 powstal po to, zeby
// manewrowanie i przenoszenie urzadzenia nie ustanawialo rekordow. Estymata
// roll przy zerowej predkosci wedruje o kilkanascie stopni (zmierzone na
// nieruchomym urzadzeniu: 24 -> 26 -> 35 -> 40), wiec bez bramki slad zapisywal
// czyste bledy estymaty jako przechyly.
//
// Ta klasa jest ta brakujaca wspolna regula. Karmi sie ja Z TEGO SAMEGO MIEJSCA
// I POD TYM SAMYM WARUNKIEM co rekordy przejazdu, wiec obie liczby moga sie
// roznic co najwyzej zaokragleniem.
//
// DLACZEGO MAKSIMUM, A NIE ODCZYT CHWILOWY. Punkt sladu zastepuje caly odcinek
// trasy, a korytarz zageszcza punkty wlasnie w zakretach — czyli tam, gdzie
// przechyl ma cos do powiedzenia. Probkowanie raz na sekunde gubiloby szczyt
// miedzy fixami; maksimum ze 100 Hz nie gubi go nigdy.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstdint>

#include "RideMetrics.h"

namespace motion {

class SegmentLean {
public:
    explicit SegmentLean(const RideMetricsConfig& config = {}) : config_(config) {}

    void setConfig(const RideMetricsConfig& config) { config_ = config; }

    /// Nowa estymata przechylu [stopnie, dodatni = w prawo].
    ///
    /// Wolac z pelna czestotliwoscia IMU i WYLACZNIE wtedy, gdy wolno zapisywac
    /// rekordy — ten sam warunek, co dla RideMetrics::update(). Zawolanie przy
    /// zamknietej bramce predkosci przywrociloby dokladnie ten blad, dla ktorego
    /// ta klasa powstala.
    void update(float rollDeg);

    /// Oddaje najwiekszy przechyl odcinka i zaczyna nowy.
    /// Zero znaczy "na tym odcinku nie bylo czego zapisac".
    int8_t take();

    /// Zaczyna odcinek od nowa, bez oddawania wyniku.
    void reset() { best_ = 0.0f; }

    /// Podglad bez zerowania — do diagnostyki.
    float peekDeg() const { return best_; }

private:
    RideMetricsConfig config_{};
    /// Najwieksza dotad wartosc ZE ZNAKIEM. Strona zakretu jest tak samo
    /// istotna jak kat, wiec nie trzymamy samej wartosci bezwzglednej.
    float best_ = 0.0f;
};

}  // namespace motion
