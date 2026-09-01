#include "SetupPortal.h"

#include <WiFi.h>

#include <cstring>

#include "PortalIdentity.h"

namespace net {
namespace {

constexpr uint16_t kDnsPort = 53;
const IPAddress kApAddress(192, 168, 4, 1);

/// Styl w jednym miejscu i bez zewnetrznych plikow: telefon w garazu bywa bez
/// internetu, wiec strona nie moze niczego dociagac z sieci.
constexpr const char* kStyle = R"CSS(
<style>
:root{color-scheme:dark}
body{margin:0;padding:24px 16px;background:#111418;color:#e8eaed;
 font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
main{max-width:420px;margin:0 auto}
h1{font-size:20px;margin:0 0 4px}
p.sub{margin:0 0 24px;color:#9aa0a6;font-size:14px}
label{display:block;margin:16px 0 6px;font-size:14px;color:#9aa0a6}
input,select{width:100%;box-sizing:border-box;padding:12px;border-radius:8px;
 border:1px solid #3c4043;background:#1e2126;color:#e8eaed;font-size:16px}
button{width:100%;margin-top:24px;padding:14px;border:0;border-radius:8px;
 background:#8ab4f8;color:#111418;font-size:16px;font-weight:600}
.hint{font-size:13px;color:#9aa0a6;margin-top:6px}
.ok{color:#81c995}.err{color:#f28b82}
</style>
)CSS";

/// Escapowanie do atrybutu HTML. Nazwa sieci pochodzi z eteru, wiec moze
/// zawierac cudzyslow albo nawiasy katowe — sasiad nazywajacy siec
/// <script> nie ma prawa nic zepsuc w naszym formularzu.
String escapeHtml(const char* text) {
    String out;
    for (const char* p = text; *p != '\0'; ++p) {
        switch (*p) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += *p; break;
        }
    }
    return out;
}

}  // namespace

bool SetupPortal::begin(const char* deviceId, const telemetry::IntegrationConfig& current) {
    if (running_) return true;

    current_ = current;
    submitted_ = current;
    pending_ = PortalEvent::None;

    telemetry::portalSsid(deviceId, apSsid_, sizeof(apSsid_));
    telemetry::portalPassword(deviceId, apPassword_, sizeof(apPassword_));

    // Skan przed AP — patrz naglowek.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    const int found = WiFi.scanNetworks();
    scannedCount_ = 0;
    for (int i = 0; i < found && scannedCount_ < kMaxScanned; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;

        // Sieci powtarzaja sie, gdy dom ma kilka punktow dostepowych.
        bool duplicate = false;
        for (size_t j = 0; j < scannedCount_; ++j) {
            if (ssid == scanned_[j]) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        std::snprintf(scanned_[scannedCount_], sizeof(scanned_[0]), "%s", ssid.c_str());
        ++scannedCount_;
    }
    WiFi.scanDelete();

    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(kApAddress, kApAddress, IPAddress(255, 255, 255, 0));
    if (!WiFi.softAP(apSsid_, apPassword_)) {
        WiFi.mode(WIFI_OFF);
        return false;
    }

    // Kazda nazwa domeny prowadzi do urzadzenia — to jest captive portal.
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(kDnsPort, "*", kApAddress);

    server_.on("/", HTTP_GET, [this]() { handleForm(); });
    server_.on("/zapisz", HTTP_POST, [this]() { handleSave(); });
    server_.onNotFound([this]() { handleNotFound(); });
    server_.begin();

    running_ = true;
    Serial.printf("[portal] siec \"%s\", haslo \"%s\", adres %s, sieci w okolicy: %u\n", apSsid_,
                  apPassword_, kApAddress.toString().c_str(),
                  static_cast<unsigned>(scannedCount_));
    return true;
}

void SetupPortal::end() {
    if (!running_) return;

    server_.stop();
    dns_.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    running_ = false;
    Serial.println("[portal] zamkniety");
}

PortalEvent SetupPortal::handle() {
    if (!running_) return PortalEvent::None;

    dns_.processNextRequest();
    server_.handleClient();

    const PortalEvent event = pending_;
    pending_ = PortalEvent::None;
    return event;
}

uint8_t SetupPortal::clientCount() const {
    return running_ ? WiFi.softAPgetStationNum() : 0;
}

void SetupPortal::handleForm() {
    String page = F("<!doctype html><html lang=pl><head><meta charset=utf-8>"
                    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                    "<title>Motusy Moto Box</title>");
    page += kStyle;
    page += F("</head><body><main><h1>Konfiguracja Moto Boxa</h1>"
              "<p class=sub>Sieć domowa i token konta ze strony motusy.top</p>"
              "<form method=post action=/zapisz>");

    page += F("<label for=siec>Sieć WiFi</label>");
    if (scannedCount_ > 0) {
        page += F("<select id=siec name=siec>");
        bool currentListed = false;
        for (size_t i = 0; i < scannedCount_; ++i) {
            const String name = escapeHtml(scanned_[i]);
            const bool selected = std::strcmp(scanned_[i], current_.ssid) == 0;
            if (selected) currentListed = true;
            page += "<option value=\"" + name + "\"" + (selected ? " selected" : "") + ">" + name +
                    "</option>";
        }
        // Siec zapisana, ale w tej chwili poza zasiegiem, nie moze zniknac
        // z formularza — inaczej zapis czegokolwiek by ja skasowal.
        if (!currentListed && current_.hasNetwork()) {
            const String name = escapeHtml(current_.ssid);
            page += "<option value=\"" + name + "\" selected>" + name + " (poza zasięgiem)</option>";
        }
        page += F("</select>");
    } else {
        page += "<input id=siec name=siec value=\"" + escapeHtml(current_.ssid) +
                "\" placeholder=\"nazwa sieci\">";
    }

    page += F("<label for=haslo>Hasło sieci</label>"
              "<input id=haslo name=haslo type=password placeholder=\"\">"
              "<p class=hint>Puste pole = bez zmian.</p>");

    page += F("<label for=token>Token konta</label>"
              "<input id=token name=token placeholder=\"\">"
              "<p class=hint>Puste pole = bez zmian. Token skopiuj ze strony.</p>");

    page += F("<button type=submit>Zapisz i sprawdź</button>"
              "</form></main></body></html>");

    server_.send(200, "text/html; charset=utf-8", page);
}

void SetupPortal::handleSave() {
    telemetry::IntegrationConfig next = current_;
    String problem;

    const String ssid = server_.arg("siec");
    if (!ssid.isEmpty() && !telemetry::setSsid(next, ssid.c_str())) {
        problem = F("Nazwa sieci jest za długa albo zawiera niedozwolone znaki.");
    }

    // Puste pole znaczy "bez zmian", nie "wyczyść" — inaczej poprawienie samego
    // tokena kasowaloby haslo do sieci przy okazji.
    const String password = server_.arg("haslo");
    if (problem.isEmpty() && !password.isEmpty() &&
        !telemetry::setPassword(next, password.c_str())) {
        problem = F("Hasło jest za długie albo zawiera niedozwolone znaki.");
    }

    const String token = server_.arg("token");
    if (problem.isEmpty() && !token.isEmpty() && !telemetry::setToken(next, token.c_str())) {
        problem = F("Token zawiera spację albo znak, którego tam być nie może — "
                    "skopiuj go jeszcze raz w całości.");
    }

    if (problem.isEmpty() && !next.isComplete()) {
        problem = F("Brakuje nazwy sieci albo tokena.");
    }

    String page = F("<!doctype html><html lang=pl><head><meta charset=utf-8>"
                    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
                    "<title>Motusy Moto Box</title>");
    page += kStyle;
    page += F("</head><body><main>");

    if (!problem.isEmpty()) {
        page += F("<h1 class=err>Nie zapisano</h1><p>");
        page += problem;
        page += F("</p><p><a href=\"/\" style=\"color:#8ab4f8\">Wróć do formularza</a></p>");
        page += F("</main></body></html>");
        server_.send(200, "text/html; charset=utf-8", page);
        return;
    }

    submitted_ = next;
    pending_ = PortalEvent::Submitted;

    // Sprawdzenie tokena wymaga polaczenia z siecia domowa, a wtedy punkt
    // dostepowy gasnie i telefon traci lacznosc. Dlatego wynik proby pokazuje
    // EKRAN URZADZENIA — obiecywanie go w przegladarce skonczyloby sie
    // zakrecona strona, ktorej nikt juz nie wczyta.
    page += F("<h1 class=ok>Zapisano</h1>"
              "<p>Urządzenie sprawdza teraz połączenie z siecią i token.</p>"
              "<p><b>Wynik zobaczysz na ekranie urządzenia.</b></p>"
              "<p class=sub>Sieć MOTOBOX za chwilę zniknie — tak ma być.</p>"
              "</main></body></html>");
    server_.send(200, "text/html; charset=utf-8", page);
}

void SetupPortal::handleNotFound() {
    // Kazde zapytanie prowadzi do formularza: to jest zaczep, ktory sprawia,
    // ze telefon sam otwiera okno konfiguracji po polaczeniu z siecia.
    server_.sendHeader("Location", String("http://") + kApAddress.toString() + "/", true);
    server_.send(302, "text/plain", "");
}

}  // namespace net
