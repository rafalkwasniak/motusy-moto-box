// Motusy Moto Box — kiedy probowac wysylki i co robic po nieudanej probie.
//
// Radio jest najdrozsza rzecza, jaka to urzadzenie potrafi zrobic: polaczenie
// z siecia i handshake TLS to kilkanascie sekund pracy przy ~100 mA. Bateria
// ma 250 mAh, wiec harmonogram wysylki jest w rownym stopniu decyzja
// energetyczna, co siecowa.
//
// KIEDY WOLNO PROBOWAC — tylko gdy urzadzenie ma prad z motocykla albo wlasnie
// go stracilo i jeszcze nie zasnelo (okno po zgaszeniu stacyjki). Motocykl stoi
// wtedy w garazu, czyli w zasiegu sieci domowej, a przejazd jest swiezy.
// W czuwaniu urzadzenie NIE budzi sie po to, zeby wysylac — dwa dni czuwania
// sa wazniejsze niz wynik dostarczony o poranek wczesniej.
//
// PO NIEUDANEJ PROBIE odstep rosnie dwukrotnie. Motocykl pod sklepem nie ma
// sieci domowej i zadna liczba prob tego nie zmieni; kazda kosztuje prad.
//
// PO ODMOWIE TOKENA (401) urzadzenie przestaje probowac CALKIEM, do zmiany
// konfiguracji. Bez tego zle przepisany token oznaczalby budzenie radia
// w kolko az do rozladowania baterii — i to bez zadnej szansy na powodzenie.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstdint>

namespace telemetry {

/// Wynik proby wysylki widziany oczami harmonogramu.
enum class UploadOutcome {
    /// 200 — serwer odpowiedzial i przyjal (choćby zero przejazdow).
    Success,
    /// 401/403 — token nieprawidlowy albo odwolany. Ponawianie nie ma sensu.
    AuthRejected,
    /// Wszystko inne: brak sieci, timeout, 5xx, 429, 422.
    TemporaryFailure,
};

struct UploadSchedulerConfig {
    /// Odstep po pierwszej nieudanej probie.
    uint32_t firstBackoffMs = 30UL * 1000UL;
    /// Gorna granica odstepu — dalsze podwajanie nic nie wnosi.
    uint32_t maxBackoffMs = 15UL * 60UL * 1000UL;
};

class UploadScheduler {
public:
    UploadScheduler() = default;
    explicit UploadScheduler(const UploadSchedulerConfig& config) : cfg_(config) {}

    /// Czy teraz jest moment na probe wysylki.
    ///
    /// @param configured    czy jest siec i token
    /// @param hasPending    czy cokolwiek czeka w kolejce
    /// @param radioAllowed  czy stan urzadzenia pozwala wlaczyc radio
    ///                      (jazda albo okno po zgaszeniu stacyjki)
    bool shouldAttempt(bool configured, bool hasPending, bool radioAllowed, uint32_t nowMs) const;

    /// Wynik zakonczonej proby.
    void onOutcome(UploadOutcome outcome, uint32_t nowMs);

    /// Zmiana sieci albo tokena zdejmuje blokade po odmowie — uzytkownik
    /// wlasnie zrobil jedyna rzecz, ktora mogla pomoc.
    void onConfigChanged();

    /// Czy harmonogram stoi z powodu odmowy tokena.
    bool isBlocked() const { return blocked_; }

    /// Ile prob z rzedu sie nie powiodlo (zerowane przez powodzenie).
    uint32_t failures() const { return failures_; }

    /// Ile jeszcze czekamy do nastepnej proby [ms]; 0 gdy nic nie blokuje.
    uint32_t msUntilNextAttempt(uint32_t nowMs) const;

private:
    UploadSchedulerConfig cfg_;
    bool blocked_ = false;
    bool waiting_ = false;
    uint32_t nextAttemptMs_ = 0;
    uint32_t failures_ = 0;
};

}  // namespace telemetry
