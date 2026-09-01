#include "Uplink.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cstring>

#include "RootCert.h"
#include "TelemetryJson.h"
#include "config.h"

namespace net {
namespace {

/// Kod HTTP na status zrozumialy dla harmonogramu.
UplinkStatus classify(int httpCode) {
    if (httpCode == 200) return UplinkStatus::Ok;
    if (httpCode == 401 || httpCode == 403) return UplinkStatus::AuthRejected;
    if (httpCode > 0) return UplinkStatus::ServerError;
    // HTTPClient oddaje wartosci ujemne dla bledow polaczenia i TLS.
    return UplinkStatus::TransportError;
}

/// Wspolny naglowek kazdego zadania.
void addHeaders(HTTPClient& http, const char* token) {
    String authorization = "Bearer ";
    authorization += token;

    http.addHeader("Authorization", authorization);
    http.addHeader("Content-Type", "application/json");
    http.setUserAgent(String("MotusyMotoBox/") + cfg::kFirmwareVersion);
    http.setTimeout(cfg::kHttpTimeoutMs);
    http.setConnectTimeout(cfg::kHttpTimeoutMs);
    // Przekierowania sa dla przegladarek. Urzadzenie ma jeden adres i ma na nim
    // dostac odpowiedz albo blad — cicha wedrowka po redirectach zamienilaby
    // literowke w adresie w tajemnicze zawieszenie.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
}

}  // namespace

bool Uplink::connect(const telemetry::IntegrationConfig& config, uint32_t timeoutMs) {
    if (!config.hasNetwork()) return false;

    WiFi.persistent(false);  // konfiguracja zyje w NVS pod naszymi kluczami
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.begin(config.ssid, config.password);

    const uint32_t deadline = millis() + timeoutMs;
    while (WiFi.status() != WL_CONNECTED && static_cast<int32_t>(deadline - millis()) > 0) {
        delay(100);
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[wysylka] brak polaczenia z siecia \"%s\"\n", config.ssid);
        disconnect();
        return false;
    }

    Serial.printf("[wysylka] polaczono z \"%s\", RSSI %d dBm, IP %s\n", config.ssid, WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
    return true;
}

void Uplink::disconnect() {
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
}

bool Uplink::isConnected() const { return WiFi.status() == WL_CONNECTED; }

int Uplink::rssi() const { return isConnected() ? WiFi.RSSI() : 0; }

UplinkStatus Uplink::ping(const char* token) {
    if (!isConnected()) return UplinkStatus::NoNetwork;

    WiFiClientSecure client;
    client.setCACert(kRootCertPem);

    HTTPClient http;
    if (!http.begin(client, cfg::kApiPingUrl)) return UplinkStatus::TransportError;
    addHeaders(http, token);

    const int code = http.GET();
    http.end();

    Serial.printf("[wysylka] ping -> %d\n", code);
    return classify(code);
}

UploadResult Uplink::postRides(const char* token, const char* payload) {
    UploadResult result;
    if (!isConnected()) {
        result.status = UplinkStatus::NoNetwork;
        return result;
    }

    WiFiClientSecure client;
    client.setCACert(kRootCertPem);

    HTTPClient http;
    if (!http.begin(client, cfg::kApiRidesUrl)) {
        result.status = UplinkStatus::TransportError;
        return result;
    }
    addHeaders(http, token);

    // HTTPClient chce wskaznika bez const, choc tresci nie zmienia. Rzutowanie
    // jest tu bezpieczniejsze niz wersja przyjmujaca String, ktora skopiowalaby
    // caly czterokilobajtowy bufor na sterte.
    result.httpCode = http.POST(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(payload)),
                                std::strlen(payload));
    result.status = classify(result.httpCode);

    if (result.status == UplinkStatus::Ok) {
        const String body = http.getString();
        uint32_t accepted = 0;
        if (telemetry::parseAcceptedThrough(body.c_str(), accepted)) {
            result.acceptedThrough = accepted;
            result.hasAccepted = true;
        } else {
            Serial.printf("[wysylka] odpowiedz 200 bez accepted_through: %s\n", body.c_str());
        }
    }

    http.end();
    Serial.printf("[wysylka] rides -> %d, accepted_through %s%u\n", result.httpCode,
                  result.hasAccepted ? "" : "(brak) ", static_cast<unsigned>(result.acceptedThrough));
    return result;
}

}  // namespace net
