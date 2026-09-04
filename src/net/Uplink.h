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

#include <Stream.h>

#include <cstddef>
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

/// Co zrobic ze sladem po probie wysylki.
///
/// OSOBNE OD UplinkStatus I TO JEST SEDNO: dla wynikow przejazdu 422 znaczy
/// "awaria, ponow", a dla sladu "plik jest trwale zepsuty, skasuj go".
/// Ta sama liczba, przeciwne dzialanie — pomylka kosztuje albo bezpowrotnie
/// utracony slad, albo radio budzace sie w kolko az do rozladowania baterii
/// (docs/api-jak-wysylac.md §6).
enum class TrackOutcome {
    /// 200 — serwer ma slad. Plik do skasowania.
    Delivered,
    /// 413, 415 przy tekstowym typie, 422 — ponowienie nigdy nie pomoze.
    /// Plik takze do skasowania, ale bez zapisu na serwerze.
    Discard,
    /// 401/403 — wysylka wstrzymana do zmiany konfiguracji.
    AuthRejected,
    /// 429, 5xx, brak sieci, timeout — zostawic plik i sprobowac pozniej.
    Retry,
};

struct TrackResult {
    TrackOutcome outcome = TrackOutcome::Retry;
    int httpCode = 0;
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

    /// POST sladu prosto ze strumienia, bez ladowania go do RAM — piecdziesiat
    /// kilobajtow obok stosu TLS (30-45 kB przy mbedtls) nie zmiescilyby sie.
    ///
    /// Adres sklada sie z `device_id` i numeru przejazdu. `deviceId` MUSI byc
    /// malymi literami: trasa po stronie serwera dopuszcza wylacznie
    /// [0-9a-f]{12}, wiec wielka litera daje 404 bez zadnej wskazowki.
    TrackResult postTrack(const char* token, const char* deviceId, uint32_t seq, Stream& body,
                          size_t length);

    /// Moc sygnalu ostatniego polaczenia [dBm]; 0 gdy nie polaczono.
    int rssi() const;
};

}  // namespace net
