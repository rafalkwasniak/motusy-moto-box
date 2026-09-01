// Motusy Moto Box — polaczenie z siecia domowa i rozmowa z API (K5).
//
// Cala warstwa radiowa zamknieta w jednym miejscu: reszta firmware widzi tylko
// "udalo sie / nie udalo sie / token zly". Decyzja KIEDY wysylac nalezy do
// telemetry::UploadScheduler (czysty C++, testowany), tutaj jest samo JAK.
//
// RADIO WLACZANE NA CZAS OPERACJI I OD RAZU GASZONE. WiFi na ESP32 to ~100 mA;
// zostawione wlaczone "na wszelki wypadek" skrociloby czuwanie z dwoch dni do
// kilku godzin. Kazda wysylka to pelny cykl: wlacz, polacz, wyslij, wylacz.
//
// POLACZENIE JEST BLOKUJACE. Przez te kilkanascie sekund urzadzenie nie
// probkuje IMU — dlatego wysylka odbywa sie wylacznie wtedy, gdy nie ma czego
// mierzyc: po zgaszeniu stacyjki albo na postoju z wlaczonym zaplonem.

#pragma once

#include <cstdint>

#include "IntegrationConfig.h"

namespace net {

enum class UplinkStatus {
    /// Serwer odpowiedzial 200.
    Ok,
    /// 401/403 — token nieprawidlowy albo odwolany.
    AuthRejected,
    /// Nie udalo sie polaczyc z siecia domowa (zly SSID, zle haslo, brak zasiegu).
    NoNetwork,
    /// Polaczenie bylo, ale serwer odpowiedzial bledem (5xx, 429, 422).
    ServerError,
    /// TLS, DNS, timeout — warstwa transportowa.
    TransportError,
};

struct UploadResult {
    UplinkStatus status = UplinkStatus::TransportError;
    /// Kod HTTP albo wartosc ujemna z HTTPClient przy bledzie transportu.
    int httpCode = 0;
    /// Numer ostatniego przejazdu przyjetego przez serwer.
    uint32_t acceptedThrough = 0;
    /// Czy `acceptedThrough` udalo sie odczytac z odpowiedzi. Odpowiedz 200
    /// BEZ tej liczby traktujemy jak "nic nie przyjeto" — przesuniecie
    /// znacznika w ciemno skasowaloby przejazdy, ktorych serwer nie ma.
    bool hasAccepted = false;
};

class Uplink {
public:
    /// Wlacza radio i laczy z siecia domowa. Blokujace.
    /// @return false gdy nie udalo sie w zadanym czasie
    bool connect(const telemetry::IntegrationConfig& config, uint32_t timeoutMs);

    /// Rozlacza i WYLACZA radio. Bezpieczne przy braku polaczenia.
    void disconnect();

    bool isConnected() const;

    /// GET /api/v1/ping — czy serwer uznaje ten token. Wymaga polaczenia.
    UplinkStatus ping(const char* token);

    /// POST /api/v1/rides z gotowa trescia JSON. Wymaga polaczenia.
    UploadResult postRides(const char* token, const char* payload);

    /// Moc sygnalu ostatniego polaczenia [dBm]; 0 gdy nie polaczono.
    int rssi() const;
};

}  // namespace net
