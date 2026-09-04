// Motusy Moto Box — punkt dostepowy z formularzem konfiguracji (K4).
//
// Ekran 240x135 i dwa przyciski nie sluza do wpisywania hasel. Urzadzenie
// stawia wiec na chwile wlasna siec WiFi, a wlasciciel wypelnia formularz
// na telefonie — tam, gdzie ma klawiature i schowek z tokenem.
//
// SIEC ZYJE TYLKO Z EKRANEM INTEGRACJA. Wejscie na ekran ja stawia, wyjscie
// gasi. Punkt dostepowy dzialajacy w tle bylby staleym zaproszeniem dla
// kazdego w zasiegu i jadl bateria.
//
// ADRES 192.168.4.1 JEST NA EKRANIE, mimo ze przekierowanie DNS (captive
// portal) zwykle otwiera strone samo. "Zwykle" jest tu slowem kluczowym:
// Android z prywatnym DNS i iOS z Private Relay potrafia je zignorowac,
// a wtedy jedynym wyjsciem jest adres przepisany recznie.
//
// SKAN SIECI PRZED PODNIESIENIEM AP: lista sasiedzkich sieci w formularzu
// oszczedza literowki w nazwie, ale skanowanie przy dzialajacym punkcie
// dostepowym rozlacza podlaczony telefon. Dlatego skanujemy raz, zanim
// ktokolwiek zdazy sie polaczyc.

#pragma once

#include <DNSServer.h>
#include <WebServer.h>

#include <cstdint>

#include "IntegrationConfig.h"

namespace net {

enum class PortalEvent {
    None,
    /// Formularz zostal wyslany i przyjety — konfiguracja czeka w submitted().
    Submitted,
};

class SetupPortal {
public:
    /// Skanuje sieci, stawia punkt dostepowy i serwer HTTP.
    /// @param deviceId identyfikator urzadzenia (nazwa i haslo sieci)
    /// @param current  biezaca konfiguracja — wypelnia formularz
    /// @param trackEnabled biezacy stan zapisu sladu — ustawia checkbox
    bool begin(const char* deviceId, const telemetry::IntegrationConfig& current,
               bool trackEnabled);

    /// Gasi serwer, DNS i punkt dostepowy.
    void end();

    /// Obsluga zadan. Wolac czesto — to jest petla portalu.
    PortalEvent handle();

    bool isRunning() const { return running_; }

    /// Nazwa i haslo sieci do pokazania na ekranie urzadzenia.
    const char* apSsid() const { return apSsid_; }
    const char* apPassword() const { return apPassword_; }

    /// Ilu klientow jest podlaczonych. Dopoki ktos jest, ekran nie gasnie.
    uint8_t clientCount() const;

    /// Konfiguracja zlozona przez formularz. Wazna po zdarzeniu Submitted.
    const telemetry::IntegrationConfig& submitted() const { return submitted_; }

    /// Stan checkboxa sladu z formularza. Wazny po zdarzeniu Submitted.
    /// Trzymany osobno od IntegrationConfig, bo tamta struktura opisuje siec
    /// i token, a slad jest ustawieniem urzadzenia, nie integracji.
    bool submittedTrackEnabled() const { return submittedTrack_; }

private:
    void handleForm();
    void handleSave();
    void handleNotFound();

    WebServer server_{80};
    DNSServer dns_;
    bool running_ = false;
    PortalEvent pending_ = PortalEvent::None;

    char apSsid_[16] = {};
    char apPassword_[16] = {};

    /// Konfiguracja pokazywana w formularzu i ta zlozona przez uzytkownika.
    telemetry::IntegrationConfig current_;
    telemetry::IntegrationConfig submitted_;

    bool currentTrack_ = false;
    bool submittedTrack_ = false;

    /// Nazwy sieci znalezione przed podniesieniem AP.
    static constexpr size_t kMaxScanned = 12;
    char scanned_[kMaxScanned][33] = {};
    size_t scannedCount_ = 0;
};

}  // namespace net
