// Motusy Moto Box — punkt wejscia firmware.
//
// Zaimplementowane:
//   E2  start urzadzenia, ekran startowy, BMI270, estymacja orientacji, dwa widoki
//   E5  pamiec nieulotna: wyniki, kalibracja, stan alarmu
//   E7  przycisk — cztery progi czasowe (§22, §23)
//   E6  maszyna stanow zasilania — wykrywanie stacyjki, gaszenie ekranu
//   E8  alarm: detekcja ruchu, sygnalizacja glosnikiem, light sleep w czuwaniu
//   E3  rejestrator surowych danych IMU (wlaczany flaga MMB_RAW_LOGGER)
//   K1-K3 wysylka wynikow: format, kolejka, ekran INTEGRACJA
//   K3a konfiguracja integracji komendami przez USB (SIEC=, HASLO=, TOKEN=)
//   K4  punkt dostepowy i formularz konfiguracji na telefonie
//   K5  klient HTTPS: laczenie z siecia domowa, harmonogram, wysylka
//   GPS parser NMEA, predkosc maksymalna do wynikow i do filtru orientacji,
//       bramka predkosci (§16), znacznik czasu przejazdu (recorded_at)
//
// Pozostaje:
//   slad trasy (GPX), strojenie filtrow na danych z jazdy, ew. deep sleep

#include <Arduino.h>
#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include "AlarmEngine.h"
#include "ButtonFsm.h"
#include "ConfigCommand.h"
#include "DeviceStateMachine.h"
#include "PortalIdentity.h"
#include "Orientation.h"
#include "RideClock.h"
#include "RideMetrics.h"
#include "SpeedGate.h"
#include "TelemetryJson.h"
#include "UploadQueue.h"
#include "UploadScheduler.h"
#include "config.h"
#include "hal/DeviceId.h"
#include "hal/GpsSource.h"
#include "hal/I2cScan.h"
#include "hal/ImuSource.h"
#include "hal/PowerSource.h"
#include "hal/Store.h"
#include "net/SetupPortal.h"
#include "net/Uplink.h"
#if MMB_RAW_LOGGER
#include "log/RawLogger.h"
#endif
#include "ui/HardwareView.h"
#include "ui/HoldPrompt.h"
#include "ui/IntegrationView.h"
#include "ui/LiveView.h"
#include "ui/MainScreen.h"
#include "ui/ScreenBuffer.h"
#include "ui/SplashScreen.h"
#include "ui/Text.h"

namespace {

motion::Orientation g_orientation;
motion::RideMetrics g_metrics;
motion::MountCalibration g_mount;
motion::RideHistory g_history;
/// Czas trwania biezacego przejazdu — od pierwszego do ostatniego ruchu.
motion::RideClock g_rideClock;
/// §16 — pomiary zapisujemy dopiero powyzej progu predkosci. Bez tego przechyl
/// przy manewrowaniu ustanawia rekord calej sesji.
motion::SpeedGate g_speedGate;
/// Czy biezacy przejazd trafil juz do historii — patrz archiveCurrentRide().
bool g_rideArchived = false;

/// Numeracja przejazdow i znacznik wyslania na motusy.top.
telemetry::UploadQueue g_queue;
/// Kiedy wolno wlaczyc radio i jak dlugo czekac po nieudanej probie.
telemetry::UploadScheduler g_scheduler;

net::Uplink g_uplink;
net::SetupPortal g_portal;
/// Siec domowa i token konta — wpisywane raz, komendami przez USB (K3a),
/// docelowo przez formularz na telefonie (K4). Ekran INTEGRACJA je pokazuje.
telemetry::IntegrationConfig g_integration;

hal::ImuSource g_imu;
hal::Store g_store;
hal::I2cScan g_i2c;

hal::GpsSourceConfig makeGpsConfig() {
    hal::GpsSourceConfig config;
    config.rxPin = cfg::kGpsRxPin;
    config.txPin = cfg::kGpsTxPin;
    config.probeMs = cfg::kGpsProbeMs;
    config.fixMaxAgeMs = cfg::kGpsFixMaxAgeMs;
    config.quality.minSatellites = cfg::kGpsMinSatellites;
    config.quality.maxHdop = cfg::kGpsMaxHdop;
    return config;
}

hal::GpsSource g_gps{makeGpsConfig()};

hal::PowerSourceConfig makePowerConfig() {
    hal::PowerSourceConfig config;
    config.chargePulseHoldMs = cfg::kChargePulseHoldMs;
    return config;
}

hal::PowerSource g_power{makePowerConfig()};

#if MMB_RAW_LOGGER
rawlog::RawLogger g_logger;
#endif

ui::ScreenBuffer g_buffer;
ui::MainScreen g_mainScreen;
ui::LiveView g_liveView;
ui::HardwareView g_hardwareView;
ui::HoldPrompt g_holdPrompt;
ui::IntegrationView g_integrationView;

// ── Role przyciskow ────────────────────────────────────────────────────────
// KEY1 jest wygodniejszy w dosiegu, wiec obsluguje czynnosc najczestsza:
// ogladanie wynikow i archiwum. KEY2 to przycisk akcji — rzadszych, ale
// waznych i celowo wymagajacych swiadomego przytrzymania.
//
//   KEY1 (widok)  klik: wyniki -> strony archiwum -> wyniki
//                 hold: widoki serwisowe (diagnostyka/sprzet)
//   KEY2 (akcja)  klik: przelaczenie alarmu
//                 2 s:  reset wynikow
//                 4 s:  kalibracja montazu
//                 6 s:  integracja ze strona (ostatnia pozycja — raz ustawiona
//                       i zapomniana, wiec najdalej od codziennych akcji)
//
// Zmiana przypisania to podmiana tych dwoch funkcji.
m5::Button_Class& viewButton() { return M5.BtnA; }
m5::Button_Class& actionButton() { return M5.BtnB; }

/// Widok wynikow -> strony historii (po 2 przejazdy) -> wyniki.
enum class ViewMode { Results, History, Diagnostics, Hardware };
ViewMode g_view = ViewMode::Results;
/// Biezaca strona historii, 0 = dwa najnowsze przejazdy.
size_t g_historyPage = 0;

/// Liczba stron historii: tylko strony, ktore maja cokolwiek do pokazania.
size_t historyPageCount() {
    const size_t count = g_history.count();
    if (count == 0) return 1;
    return (count + 1) / 2;
}

/// Drabinka przycisku (§22). Kolejnosc szczebli wynika z czestosci uzycia —
/// uzasadnienie przy progach w config.h.
input::ButtonFsmConfig makeButtonConfig() {
    input::ButtonFsmConfig config;
    config.rungCount = 5;
    config.rungs[0] = {0, input::ButtonAction::Alarm};
    config.rungs[1] = {cfg::kButtonTrackHoldMs, input::ButtonAction::Track};
    config.rungs[2] = {cfg::kButtonResetHoldMs, input::ButtonAction::Reset};
    config.rungs[3] = {cfg::kButtonCalibrationHoldMs, input::ButtonAction::Calibration};
    config.rungs[4] = {cfg::kButtonIntegrationHoldMs, input::ButtonAction::Integration};
    return config;
}

input::ButtonFsm g_button{makeButtonConfig()};

// ── Tryb stanowiskowy (MMB_BENCH) ──────────────────────────────────────────
// Pozwala testowac sciezke alarmu przy WPIETYM USB: po 10 s od startu
// zasilanie jest raportowane jako nieobecne, odliczanie trwa 20 s, a na
// port szeregowy leci telemetria detekcji. MMB_BENCH_SLEEP wlacza dodatkowo
// prawdziwy light sleep (peda serial — werdykt daje wtedy piszczenie).
#ifdef MMB_BENCH
constexpr uint32_t kBenchUnplugAtMs = 10000;
bool effectiveExternalPower() { return millis() < kBenchUnplugAtMs; }
#endif

/// Progi maszyny stanow — czasy z §18 i z ustalen o odpornosci na rozruch.
state::DeviceStateConfig makeStateConfig() {
    state::DeviceStateConfig config;
    config.armingDelayMs = cfg::kArmingDelayMs;
    config.powerLossConfirmMs = cfg::kPowerLossConfirmMs;
    config.powerReturnConfirmMs = cfg::kPowerReturnConfirmMs;
    // Antydatowanie o czas detekcji. Napiecie wejsciowe jest odczytem
    // natychmiastowym, wiec zostaje samo potwierdzenie czasowe.
    config.detectionLatencyMs = cfg::kPowerLossConfirmMs;
#ifdef MMB_BENCH
    config.armingDelayMs = 20000;
    config.detectionLatencyMs = 0;
#endif
    return config;
}

state::DeviceStateMachine g_deviceState{makeStateConfig()};
bool g_screenOn = true;

guard::AlarmEngine g_alarm;
motion::ImuSample g_lastSample;
bool g_haveNewSample = false;
bool g_haveSampleEver = false;
bool g_sirenPlaying = false;
uint16_t g_sirenFreqHz = 0;
/// Tor audio (wzmacniacz) wlaczony na czas sygnalizacji.
bool g_audioActive = false;
/// Klik, ktory obudzil ekran albo wyciszyl alarm, nie wykonuje swojej
/// normalnej funkcji. Osobno dla kazdego przycisku, zeby nacisniecie jednego
/// nie polykalo klikniecia drugiego.
bool g_swallowView = false;
bool g_swallowAction = false;

// Pomiar czuwania. Wartosci sa ZAMRAZANE przy obudzeniu ekranu — inaczej
// licznik czasu bieglby dalej, a sen juz nie przyrastal (ekran swieci), przez
// co odczyt spadalby w oczach i nie dalo by sie go spokojnie przeczytac.
uint64_t g_sleepUs = 0;
uint64_t g_awakeUs = 0;
uint32_t g_wakeCount = 0;
int64_t g_lastWakeEndUs = 0;
uint32_t g_standbyStartMs = 0;
int g_frozenSleepPercent = 0;
uint32_t g_frozenAwakeUs = 0;
uint32_t g_frozenStandbySeconds = 0;

bool g_imuAvailable = false;
bool g_alarmEnabled = true;
/// Zapis sladu trasy. WYLACZONY domyslnie i to jest cala roznica wobec alarmu:
/// alarm ma chronic motocykl, wiec brak ustawienia nie moze go zostawic bez
/// ochrony; slad zapisuje trase, wiec brak ustawienia nie moze go wlaczyc.
bool g_trackEnabled = false;
const char* g_storageStatus = "PAMIEC - BLAD";
char g_i2cStatus[32] = "I2C - BRAK SKANU";

uint32_t g_lastSampleMicros = 0;
uint32_t g_lastDisplayMs = 0;
uint32_t g_lastAutosaveMs = 0;

/// Komunikat pelnoekranowy — potwierdzenia akcji i kalibracja.
///
/// Napisy dobieraja font do wlasnej dlugosci, zeby nigdy nie wyjsc poza ekran:
/// tresci komunikatow zmieniaja sie czesciej niz uklad, wiec dobieranie fontu
/// recznie i tak by sie rozjechalo.
void drawMessage(const char* title, const char* detail, uint16_t accent) {
    m5gfx::LovyanGFX* gfx = g_buffer.gfx();
    gfx->fillScreen(ui::color::kBackground);
    gfx->setTextDatum(middle_center);

    const int centerX = ui::layout::kScreenWidth / 2;
    const int maxWidth = ui::layout::kContentWidth;
    const bool hasDetail = detail != nullptr && detail[0] != '\0';

    gfx->setTextColor(accent);
    ui::text::drawFitted(gfx, title, centerX, hasDetail ? 58 : 67, maxWidth);

    if (hasDetail) {
        // Podpis zawsze najmniejszym fontem — ma uzupelniac tytul, nie z nim
        // konkurowac. Drobny tekst pasuje do skali tego ekranu.
        gfx->setFont(&fonts::Font0);
        gfx->setTextColor(ui::color::kMuted);
        gfx->drawString(detail, centerX, 92);
    }

    g_buffer.present();
}

/// Przenosi biezacy przejazd do historii — raz. Wolane przy zaniku zasilania
/// (przejazd skonczony) i tuz przed nowa sesja (siatka bezpieczenstwa, gdyby
/// zanik nigdy nie zostal potwierdzony, np. po restarcie na baterii).
void archiveCurrentRide() {
    if (g_rideArchived) return;
    // Znacznik czasu bierzemy z GPS-a — urzadzenie nie ma RTC. Zero znaczy
    // "przejazd bez zasiegu satelitow" i idzie do API jako null; kolejnosc
    // przejazdow i tak wynika z numeru `seq`, nie z daty.
    if (!g_history.push(g_metrics.currentRide(), g_rideClock.seconds(),
                        g_gps.unixTime(millis()))) {
        return;  // pusty przejazd
    }
    g_rideArchived = true;

    // Numer nadajemy dokladnie tam, gdzie przejazd wchodzi do historii —
    // te dwie rzeczy musza sie zgadzac co do sztuki, bo numer wpisu wynika
    // z jego pozycji w historii.
    g_queue.onRideArchived();

    g_store.saveHistory(g_history);
    g_store.saveUploadState(g_queue.lastSeq(), g_queue.sentThrough());
    g_store.saveResults(g_metrics.overall(), g_metrics.currentRide(), g_rideArchived,
                        g_rideClock.seconds());
}

/// Zapis wynikow wedlug strategii z architektury §6.2: okresowo i tylko gdy
/// cokolwiek sie zmienilo. `force` pomija odstep czasowy — uzywane przy akcjach
/// uzytkownika i (docelowo) przy zaniku zasilania.
void saveResultsIfDirty(bool force) {
    if (!g_metrics.dirty()) return;

    const uint32_t nowMs = millis();
    if (!force && nowMs - g_lastAutosaveMs < cfg::kAutosaveIntervalMs) return;

    if (g_store.saveResults(g_metrics.overall(), g_metrics.currentRide(), g_rideArchived,
                            g_rideClock.seconds())) {
        g_metrics.clearDirty();
    }
    g_lastAutosaveMs = nowMs;
}

/// §14 — kalibracja orientacji montazu.
void runMountCalibration() {
    drawMessage("KALIBRACJA", "NIE RUSZAJ MOTOCYKLA", ui::color::kCalibration);

    motion::Vec3 sum;
    uint16_t collected = 0;
    const uint32_t deadline = millis() + 5000;

    while (collected < cfg::kCalibrationSampleCount && millis() < deadline) {
        motion::ImuSample sample;
        if (g_imu.read(sample)) {
            sum += sample.accelG;
            ++collected;
        }
        delay(2);
    }

    if (collected < cfg::kCalibrationSampleCount / 2) {
        drawMessage("BLAD", "BRAK DANYCH Z IMU", ui::color::kAlarm);
        delay(2000);
        return;
    }

    const motion::Vec3 average = sum * (1.0f / static_cast<float>(collected));
    if (g_mount.calibrateFromRest(average, cfg::kMountForwardAxis)) {
        g_orientation.setMount(g_mount);
        g_orientation.resetAngles();
        const bool saved = g_store.saveMount(g_mount);
        drawMessage("KALIBRACJA OK", saved ? "ZAPISANA" : "BLAD ZAPISU",
                    saved ? ui::color::kRiding : ui::color::kWaiting);
    } else {
        // Najczestsza przyczyna: motocykl sie poruszyl albo stoi na bocznej nozce.
        drawMessage("KALIBRACJA", "SPROBUJ PONOWNIE", ui::color::kAlarm);
    }
    delay(2000);
}

/// §15 — reset obu zestawow wynikow. Nie dotyka kalibracji ani stanu alarmu.
void runResultsReset() {
    g_metrics.resetAll();
    // Skoro zerujemy OSTATNIA JAZDE, jej czas trwania tez zaczyna sie od nowa —
    // inaczej przejazd o zerowych rekordach mialby polgodzinny czas.
    g_rideClock.reset();
    const bool saved =
        g_store.saveResults(g_metrics.overall(), g_metrics.currentRide(), g_rideArchived,
                            g_rideClock.seconds());
    if (saved) g_metrics.clearDirty();

    drawMessage("POMIARY WYZEROWANE", saved ? "" : "BLAD ZAPISU",
                saved ? ui::color::kRiding : ui::color::kAlarm);
    delay(1500);
}

// ── Wysylka na motusy.top ──────────────────────────────────────────────────

telemetry::UploadOutcome toOutcome(net::UplinkStatus status) {
    switch (status) {
        case net::UplinkStatus::Ok: return telemetry::UploadOutcome::Success;
        case net::UplinkStatus::AuthRejected: return telemetry::UploadOutcome::AuthRejected;
        default: return telemetry::UploadOutcome::TemporaryFailure;
    }
}

/// Kompletuje zalegle przejazdy i wysyla je jedna przesylka.
/// Wymaga gotowego polaczenia — laczeniem zajmuja sie wolajacy.
///
/// @param sentCount ile przejazdow poszlo w przesylce
net::UplinkStatus sendPendingRides(uint32_t& sentCount) {
    sentCount = 0;

    telemetry::RideRecord rides[telemetry::kMaxRidesPerPayload];
    const size_t count = g_queue.collect(g_history, rides, telemetry::kMaxRidesPerPayload);

    telemetry::DeviceIdentity identity;
    identity.deviceId = hal::deviceId();
    identity.firmware = cfg::kFirmwareVersion;
    identity.calibrated = g_mount.isCalibrated();

    // Bufor statyczny, nie na stosie: cztery kilobajty w zadaniu petli glownej
    // to prosta droga do przepelnienia stosu.
    static char payload[telemetry::kMaxPayloadBytes];
    const size_t length = telemetry::buildPayload(identity, rides, count, payload, sizeof(payload));
    if (length == 0) {
        Serial.println("[wysylka] nie udalo sie zlozyc przesylki");
        return net::UplinkStatus::TransportError;
    }

    const net::UploadResult result = g_uplink.postRides(g_integration.token, payload);

    // Znacznik przesuwamy WYLACZNIE na podstawie liczby od serwera. Odpowiedz
    // 200 bez tej liczby zostawia kolejke nietknieta — przejazdy wroca przy
    // nastepnej probie, a to jest zawsze lepsze niz ciche skasowanie.
    if (result.status == net::UplinkStatus::Ok && result.hasAccepted) {
        if (g_queue.confirmSentThrough(result.acceptedThrough)) {
            g_store.saveUploadState(g_queue.lastSeq(), g_queue.sentThrough());
            sentCount = static_cast<uint32_t>(count);
        } else if (count > 0) {
            // Serwer odpowiedzial 200, ale znacznik stoi w miejscu — przesylka
            // poszla w prozne. Zdarza sie, gdy najstarsze przejazdy wypadly
            // z historii, a druga strona czeka na ciaglosc od poczatku;
            // ponawianie tego samego nic nie zmieni (patrz api-telemetria.md,
            // "Przejazdy, ktorych urzadzenie juz nie ma").
            Serial.printf(
                "[wysylka] UWAGA: wyslano przejazdy %u..%u, serwer potwierdzil %u - "
                "znacznik bez zmian\n",
                static_cast<unsigned>(rides[0].seq), static_cast<unsigned>(rides[count - 1].seq),
                static_cast<unsigned>(result.acceptedThrough));
            // Cala tresc zadania na port: przy rozbieznosci miedzy urzadzeniem
            // a serwerem to jedyny dowod, ktora strona wysyla co innego,
            // niz obie strony sadza.
            Serial.printf("[wysylka] tresc zadania: %s\n", payload);
        }
    }
    return result.status;
}

/// Pelny cykl wysylki: radio wlaczone, przesylka, radio wylaczone.
/// Blokuje petle glowna na kilkanascie sekund — dlatego wolane tylko wtedy,
/// gdy nie ma czego mierzyc (patrz warunek w loop()).
void runScheduledUpload() {
    const size_t pending = g_queue.pendingCount(g_history.count());
    Serial.printf("[wysylka] proba, zaleglosci: %u\n", static_cast<unsigned>(pending));

    if (!g_uplink.connect(g_integration, cfg::kWifiConnectTimeoutMs)) {
        g_uplink.disconnect();
        g_scheduler.onOutcome(telemetry::UploadOutcome::TemporaryFailure, millis());
        return;
    }

    uint32_t sent = 0;
    const net::UplinkStatus status = sendPendingRides(sent);
    g_uplink.disconnect();

    g_scheduler.onOutcome(toOutcome(status), millis());
    if (status == net::UplinkStatus::AuthRejected) {
        Serial.println("[wysylka] token odrzucony - wysylka wstrzymana do zmiany konfiguracji");
    }
}

/// Sprawdzenie swiezo zapisanej konfiguracji: polaczenie, token, a jesli oba
/// dzialaja — od razu wysylka zaleglosci. To jest odpowiedz na pytanie
/// "czy dobrze przepisalem token", zadane w chwili, gdy uzytkownik jeszcze
/// stoi przy motocyklu z telefonem w rece.
void verifyIntegration() {
    drawMessage("SPRAWDZAM", g_integration.ssid, ui::color::kWaiting);

    if (!g_uplink.connect(g_integration, cfg::kWifiConnectTimeoutMs)) {
        g_uplink.disconnect();
        drawMessage("BRAK SIECI", "SPRAWDZ NAZWE I HASLO", ui::color::kAlarm);
        delay(3000);
        return;
    }

    const net::UplinkStatus status = g_uplink.ping(g_integration.token);

    if (status != net::UplinkStatus::Ok) {
        g_uplink.disconnect();
        if (status == net::UplinkStatus::AuthRejected) {
            drawMessage("TOKEN ODRZUCONY", "SPRAWDZ TOKEN NA STRONIE", ui::color::kAlarm);
        } else {
            drawMessage("SERWER MILCZY", "SIEC OK, SPROBUJ POZNIEJ", ui::color::kWaiting);
        }
        delay(3000);
        return;
    }

    // Skoro juz jestesmy w sieci, nie ma po co czekac na osobna okazje.
    const size_t pendingBefore = g_queue.pendingCount(g_history.count());
    uint32_t sent = 0;
    const net::UplinkStatus upload = sendPendingRides(sent);
    g_uplink.disconnect();
    g_scheduler.onOutcome(toOutcome(upload), millis());

    // Rozroznienie wazne przy pierwszym uruchomieniu: "nic nie czekalo" i
    // "czekalo, ale serwer tego nie wzial" to dwie zupelnie rozne wiadomosci,
    // a obie konczyly sie tym samym napisem.
    if (pendingBefore > 0 && sent == 0) {
        drawMessage("SERWER NIE PRZYJAL", "SPRAWDZ API", ui::color::kWaiting);
        delay(3000);
        return;
    }

    char detail[32];
    if (sent > 0) {
        std::snprintf(detail, sizeof(detail), "WYSLANO PRZEJAZDOW: %u", static_cast<unsigned>(sent));
    } else {
        std::snprintf(detail, sizeof(detail), "NIC DO WYSLANIA");
    }
    drawMessage("INTEGRACJA OK", detail, ui::color::kRiding);
    delay(3000);
}

/// Ekran INTEGRACJA — punkt dostepowy i formularz konfiguracji (K4).
///
/// Wejscie tutaj stawia siec urzadzenia, wyjscie ja gasi. Ekran pokazuje
/// nazwe sieci, haslo i adres — trzy rzeczy do przepisania do telefonu.
void runIntegrationScreen() {
    if (!g_portal.begin(hal::deviceId(), g_integration, g_trackEnabled)) {
        drawMessage("BLAD", "NIE UDALO SIE WLACZYC WIFI", ui::color::kAlarm);
        delay(2500);
        return;
    }

    // Pelna jasnosc na czas przepisywania hasla. Domyslne 160 wystarcza do
    // czytania wynikow w ruchu, ale nie do odczytania dziesieciu znakow
    // w garazu — a ten ekran zyje najwyzej kilka minut, wiec prad nie boli.
    M5.Display.setBrightness(255);

    char tokenMask[24];
    telemetry::maskToken(g_integration, tokenMask, sizeof(tokenMask));

    ui::IntegrationViewModel model;
    model.ssid = g_integration.ssid;
    model.tokenMask = tokenMask;
    model.hasNetwork = g_integration.hasNetwork();
    model.hasToken = g_integration.hasToken();
    model.configured = g_integration.isComplete();
    model.pendingUploads = static_cast<uint32_t>(g_queue.pendingCount(g_history.count()));
    model.portalRunning = true;
    model.apSsid = g_portal.apSsid();
    model.apPassword = g_portal.apPassword();

    bool submitted = false;
    uint32_t deadline = millis() + cfg::kIntegrationScreenMs;
    uint32_t lastDraw = 0;

    while (static_cast<int32_t>(deadline - millis()) > 0) {
        M5.update();
        if (viewButton().wasClicked() || actionButton().wasClicked()) break;

        if (g_portal.handle() == net::PortalEvent::Submitted) {
            submitted = true;
            // Chwila na dociagniecie strony z potwierdzeniem, zanim punkt
            // dostepowy zniknie telefonowi sprzed nosa.
            const uint32_t until = millis() + 1200;
            while (static_cast<int32_t>(until - millis()) > 0) {
                g_portal.handle();
                delay(10);
            }
            break;
        }

        const uint8_t clients = g_portal.clientCount();
        // Dopoki ktos jest przy formularzu, odliczanie rusza od nowa —
        // wpisywanie tokena na telefonie potrafi trwac.
        if (clients > 0) deadline = millis() + cfg::kIntegrationScreenMs;

        const uint32_t nowMs = millis();
        if (nowMs - lastDraw >= cfg::kDisplayRefreshMs) {
            lastDraw = nowMs;
            model.clients = clients;
            g_integrationView.draw(g_buffer, model);
        }
        delay(5);
    }

    const telemetry::IntegrationConfig next = g_portal.submitted();
    const bool nextTrack = g_portal.submittedTrackEnabled();
    g_portal.end();
    M5.Display.setBrightness(cfg::kDisplayBrightness);

    if (!submitted) return;

    // Slad zapisujemy osobno i PRZED integracja: to ustawienie urzadzenia,
    // a nie czesc konfiguracji sieci, wiec nieudany zapis tokena nie ma prawa
    // cofnac swiadomie zaznaczonej zgody na zapis trasy.
    if (nextTrack != g_trackEnabled) {
        g_trackEnabled = nextTrack;
        g_store.saveTrackEnabled(g_trackEnabled);
    }

    g_integration = next;
    const bool saved = g_store.saveIntegration(g_integration);
    // Nowa konfiguracja zdejmuje blokade po odmowie tokena — uzytkownik
    // wlasnie zrobil jedyna rzecz, ktora mogla pomoc.
    g_scheduler.onConfigChanged();

    if (!saved) {
        drawMessage("BLAD ZAPISU", "USTAWIENIA NIE PRZEZYJA RESTARTU", ui::color::kAlarm);
        delay(3000);
        return;
    }

    verifyIntegration();
}

// ── Konfiguracja integracji przez USB ──────────────────────────────────────
// Jedyne miejsce w firmware czytajace port szeregowy. Linie ida najpierw do
// konfiguracji, potem — jesli to nie jej komenda — do rejestratora surowych
// danych. Dwa niezalezne czytniki podkradalyby sobie znaki.

/// Token (do 128 znakow) plus nazwa klucza, z zapasem.
constexpr size_t kSerialLineMax = 160;
char g_serialLine[kSerialLineMax + 1];
size_t g_serialLen = 0;
/// Linia dluzsza niz bufor jest odrzucana W CALOSCI. Obcieta polowa tokena
/// zapisalaby sie jako token kompletny i wygladala na poprawna konfiguracje,
/// a serwer odpowiadalby na nia 401 bez zadnej wskazowki dlaczego.
bool g_serialTooLong = false;

/// Definicja nizej, przy obsludze przyciskow — komenda TEST konczy sie tak
/// samo jak akcja z ekranu: dluga przerwa, po ktorej pomiar czasu musi ruszyc
/// od zera.
void resumeAfterAction();

void printConfigHelp(Stream& io) {
    io.println("[konfig] SIEC=<nazwa>  HASLO=<haslo>  TOKEN=<token konta>  STAN  TEST  KASUJ");
    io.println("[gps]    GPS - stan modulu | GPS SUROWE - podglad zdan NMEA");
    io.println("[slad]   SLAD - przelacza zapis sladu trasy GPX");
}

/// Stan modulu GPS jedna linia. Pierwsza rzecz, o ktora sie pyta przy
/// "predkosc pokazuje kreski".
void printGpsStatus(Stream& io) {
    const uint32_t nowMs = millis();
    io.printf("[gps] zasilanie: %s | port: %lu baud RX=G%d | zdania: %lu ok / %lu odrzucone\n",
              g_gps.isPowered() ? "wl" : "wyl (poza jazda)",
              static_cast<unsigned long>(g_gps.baud()), g_gps.rxPin(),
              static_cast<unsigned long>(g_gps.validSentences()),
              static_cast<unsigned long>(g_gps.rejectedSentences()));
    io.printf("[gps] modul odpowiada: %s | fix: %s | satelity: %u | hdop: %.1f | predkosc: %.1f km/h\n",
              g_gps.isReceiving() ? "tak" : "NIE",
              g_gps.hasFix(nowMs) ? "tak" : "nie", static_cast<unsigned>(g_gps.satellites()),
              static_cast<double>(g_gps.hdop()), static_cast<double>(g_gps.lastSpeedKmh()));

    // Czas z modulu to jedyne zrodlo daty w urzadzeniu — bez niego przejazdy
    // ida na serwer z `recorded_at: null`.
    const uint32_t epoch = g_gps.unixTime(nowMs);
    if (epoch == 0) {
        io.println("[gps] czas: nieznany - przejazdy pojda z recorded_at: null");
    } else {
        const time_t stamp = static_cast<time_t>(epoch);
        struct tm utc;
        gmtime_r(&stamp, &utc);
        io.printf("[gps] czas UTC: %04d-%02d-%02d %02d:%02d:%02d (epoch %lu)\n",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min,
                  utc.tm_sec, static_cast<unsigned long>(epoch));
    }

    // Bramka predkosci (§16): po tym widac, czy pomiary sa w tej chwili
    // zapisywane i co o tym zadecydowalo.
    io.printf("[gps] bramka: %s (zrodlo: %s)\n",
              g_speedGate.isRecording(g_orientation.state().stationary, nowMs) ? "rejestruje"
                                                                              : "wstrzymana",
              g_speedGate.hasFreshSpeed(nowMs) ? "predkosc GPS" : "bezruch z IMU");
}

/// Stan konfiguracji BEZ sekretow: haslo tylko jako fakt, token zamaskowany.
/// Wystarczy do odpowiedzi na pytanie "czy to ten token", a wydruk z portu
/// szeregowego bywa wklejany do zgloszen.
void printIntegrationStatus(Stream& io) {
    char tokenMask[24];
    telemetry::maskToken(g_integration, tokenMask, sizeof(tokenMask));

    io.printf("[konfig] siec: %s | haslo: %s | token: %s | komplet: %s\n",
              g_integration.hasNetwork() ? g_integration.ssid : "BRAK",
              g_integration.password[0] != '\0' ? "ustawione" : "brak (siec otwarta)",
              tokenMask, g_integration.isComplete() ? "tak" : "nie");

    // Stan kolejki obok konfiguracji: bez tego "wyslalo sie czy nie" trzeba
    // zgadywac z ekranu SPRZET, a przy diagnostyce wysylki to pierwsza rzecz,
    // o ktora sie pyta.
    io.printf("[konfig] kolejka: zaleglosci %u | ostatni przejazd %u | wyslane do %u\n",
              static_cast<unsigned>(g_queue.pendingCount(g_history.count())),
              static_cast<unsigned>(g_queue.lastSeq()),
              static_cast<unsigned>(g_queue.sentThrough()));
}

/// Porownanie komendy bez rozroznienia wielkosci liter — te same zasady, co
/// w telemetry::applyConfigLine, zeby "gps" i "GPS" znaczyly to samo.
bool commandEquals(const char* line, const char* pattern) {
    for (; *line != '\0' && *pattern != '\0'; ++line, ++pattern) {
        const char c =
            (*line >= 'a' && *line <= 'z') ? static_cast<char>(*line - 'a' + 'A') : *line;
        if (c != *pattern) return false;
    }
    return *line == '\0' && *pattern == '\0';
}

void handleSerialLine(Stream& io, const char* line) {
    const telemetry::ConfigCommandResult result =
        telemetry::applyConfigLine(g_integration, line);

    if (result.field == telemetry::ConfigField::None) {
        if (line[0] == '\0') return;

        if (commandEquals(line, "GPS")) {
            printGpsStatus(io);
            return;
        }

        // Droga serwisowa do przelacznika sladu — ta sama, co przycisk (2-4 s)
        // i checkbox w portalu. Przy biurku jest najkrotsza.
        if (commandEquals(line, "SLAD")) {
            g_trackEnabled = !g_trackEnabled;
            if (!g_store.saveTrackEnabled(g_trackEnabled)) {
                io.println("[slad] BLAD ZAPISU - ustawienie nie przezyje restartu");
                return;
            }
            io.printf("[slad] zapis trasy: %s\n",
                      g_trackEnabled ? "WLACZONY" : "wylaczony (ta sama komenda wlacza)");
            return;
        }

        // Podglad surowych zdan: jedyne narzedzie, ktore odpowiada na pytanie
        // "czy modul w ogole cokolwiek mowi", gdy parser milczy.
        if (commandEquals(line, "GPS SUROWE")) {
            const bool on = !g_gps.isEchoing();
            g_gps.setEcho(on ? &io : nullptr);
            io.printf("[gps] podglad surowych zdan: %s\n",
                      on ? "WLACZONY (ta sama komenda wylacza)" : "wylaczony");
            return;
        }

#if MMB_RAW_LOGGER
        if (g_logger.handleCommand(io, line)) return;
#endif
        printConfigHelp(io);
        return;
    }

    if (!result.accepted) {
        // Konfiguracja zostaje bez zmian — mowimy o tym wprost, bo to jedyna
        // roznica miedzy "wpisalem" a "urzadzenie to przyjelo".
        io.printf("[konfig] %s: wartosc odrzucona, bez zmian\n",
                  telemetry::configFieldName(result.field));
        return;
    }

    if (result.needsSave && !g_store.saveIntegration(g_integration)) {
        io.println("[konfig] BLAD ZAPISU - ustawienie nie przezyje restartu");
        return;
    }

    if (result.field == telemetry::ConfigField::Verify) {
        if (!g_integration.isComplete()) {
            io.println("[konfig] brak kompletu - najpierw SIEC i TOKEN");
            return;
        }
        verifyIntegration();
        resumeAfterAction();
        return;
    }

    if (result.field == telemetry::ConfigField::Clear) {
        io.println("[konfig] konfiguracja skasowana");
    }
    printIntegrationStatus(io);
}

void pumpSerial(Stream& io) {
    while (io.available() > 0) {
        const char c = static_cast<char>(io.read());

        // Koniec linii to CR ALBO LF: terminale wysylaja po enterze rozne
        // rzeczy (miniterm CRLF, screen samo CR). Pusta linia z drugiej polowy
        // CRLF nic nie robi, wiec obsluga obu znakow jest darmowa.
        if (c != '\n' && c != '\r') {
            if (g_serialLen < kSerialLineMax) {
                g_serialLine[g_serialLen++] = c;
            } else {
                g_serialTooLong = true;
            }
            continue;
        }

        g_serialLine[g_serialLen] = '\0';
        const bool tooLong = g_serialTooLong;
        g_serialLen = 0;
        g_serialTooLong = false;

        if (tooLong) {
            io.println("[konfig] linia za dluga - odrzucona w calosci");
            continue;
        }
        handleSerialLine(io, g_serialLine);
    }
}

/// §22.1 — przelaczenie modulu alarmowego.
void toggleAlarm() {
    g_alarmEnabled = !g_alarmEnabled;
    g_store.saveAlarmEnabled(g_alarmEnabled);

    drawMessage(g_alarmEnabled ? "ALARM WLACZONY" : "ALARM WYLACZONY", "",
                g_alarmEnabled ? ui::color::kAlarm : ui::color::kMuted);
    delay(1200);
}

/// Przelaczenie zapisu sladu trasy. Domyslnie WYLACZONY — trasa to dane innej
/// wagi niz kat przechylu, wiec ma byc swiadoma zgoda (docs/gpx-slad-trasy.md §1).
/// Wylaczona opcja znaczy, ze nic sie nie zapisuje: zero zuzycia flasha,
/// nie tylko brak wysylki.
void toggleTrack() {
    g_trackEnabled = !g_trackEnabled;
    g_store.saveTrackEnabled(g_trackEnabled);

    drawMessage(g_trackEnabled ? "SLAD WLACZONY" : "SLAD WYLACZONY",
                g_trackEnabled ? "zapis trasy GPX" : "trasa nie jest zapisywana",
                g_trackEnabled ? ui::color::kSpeed : ui::color::kMuted);
    delay(1200);
}

/// Odtworzenie stanu z pamieci nieulotnej.
///
/// Rozroznienie wazne dla §6.1 i §17: przy starcie z zasilaniem zewnetrznym
/// stacyjka wlasnie zostala wlaczona, wiec zaczynamy NOWA SESJE i zerujemy
/// OSTATNIA JAZDE. Przy starcie na samej baterii (restart po awarii, watchdog)
/// nie bylo wlaczenia stacyjki — wyniki sesji nalezy odtworzyc, a nie skasowac.
void restoreState(bool externalPowerAtBoot) {
    hal::PersistentState state;
    const hal::LoadResult result = g_store.load(state);

    switch (result) {
        case hal::LoadResult::Restored: g_storageStatus = "PAMIEC OK"; break;
        case hal::LoadResult::Fresh: g_storageStatus = "PAMIEC - PIERWSZY START"; break;
        case hal::LoadResult::Migrated: g_storageStatus = "PAMIEC - NOWY FORMAT"; break;
        case hal::LoadResult::Failed: g_storageStatus = "PAMIEC - BLAD"; break;
    }

    g_alarmEnabled = state.alarmEnabled;
    g_trackEnabled = state.trackEnabled;
    g_history = state.history;
    g_rideArchived = state.rideArchived;
    g_queue.restore(state.lastRideSeq, state.sentThrough);
    g_integration = state.integration;

    if (state.mountCalibrated) {
        g_mount.restore(state.mountRotation);
    }
    g_orientation.setMount(g_mount);

    if (externalPowerAtBoot) {
        // Nowa sesja. Jesli poprzedni przejazd nie zdazyl trafic do historii
        // (urzadzenie padlo w trakcie), ratujemy go teraz — przed wyzerowaniem.
        // Czas trwania odtwarzamy PRZED archiwizacja, bo dotyczy jeszcze
        // tamtego przejazdu; dopiero potem zerujemy licznik.
        g_metrics.restore(state.overall, state.ride);
        g_rideClock.restore(state.rideDurationS);
        archiveCurrentRide();
        g_metrics.startNewRide();
        g_rideClock.reset();
        g_metrics.clearDirty();
        g_rideArchived = false;
    } else {
        // Restart na baterii: przejazd trwa dalej (§25).
        g_metrics.restore(state.overall, state.ride);
        g_rideClock.restore(state.rideDurationS);
    }
}

void runSplash() {
    ui::SplashScreen splash;
    splash.begin();

    struct Check {
        const char* text;
        bool ok;
    };

    static char imuLine[32];
    if (g_imuAvailable && g_i2c.imuAddress() != 0) {
        std::snprintf(imuLine, sizeof(imuLine), "IMU BMI270 OK  0x%02X", g_i2c.imuAddress());
    } else {
        std::snprintf(imuLine, sizeof(imuLine), "IMU BMI270 - BRAK");
    }

    const Check checks[] = {
        {imuLine, g_imuAvailable},
        {g_i2cStatus, g_i2c.allCriticalPresent()},
        {g_storageStatus, g_store.isAvailable()},
        {g_mount.isCalibrated() ? "KALIBRACJA OK" : "KALIBRACJA - BRAK", true},
        {cfg::kFirmwareVersion, true},
    };
    constexpr int kCheckCount = sizeof(checks) / sizeof(checks[0]);

    const uint32_t start = millis();
    int shown = -1;

    while (true) {
        const uint32_t elapsed = millis() - start;
        if (elapsed >= cfg::kSplashDurationMs) break;

        const int index = static_cast<int>(elapsed * kCheckCount / cfg::kSplashDurationMs);
        if (index != shown && index < kCheckCount) {
            shown = index;
            splash.setStatus(checks[index].text, checks[index].ok);
        }
        splash.setProgress(static_cast<float>(elapsed) /
                           static_cast<float>(cfg::kSplashDurationMs));
        delay(20);
    }
    splash.setProgress(1.0f);
}

void pumpImu() {
    motion::ImuSample sample;
    if (!g_imu.read(sample)) return;

    const uint32_t nowMicros = micros();
    if (g_lastSampleMicros == 0) {
        g_lastSampleMicros = nowMicros;
        return;
    }

    // dt z mikrosekund, nie z millis: przy 100 Hz kwantyzacja milisekundowa
    // wnosilaby 10% bledu do calkowania zyroskopu.
    const float dtSec = static_cast<float>(nowMicros - g_lastSampleMicros) / 1e6f;
    g_lastSampleMicros = nowMicros;

    g_lastSample = sample;
    g_haveNewSample = true;
    g_haveSampleEver = true;

#if MMB_RAW_LOGGER
    g_logger.log(sample);
#endif

    g_orientation.update(sample, dtSec);

    // Rekordy zbieramy WYLACZNIE w trakcie jazdy (§17, §18: po zgasnieciu
    // stacyjki wyniki pozostaja niezmienione). Bez tego warunku manipulowanie
    // urzadzeniem na parkingu — zdjecie z uchwytu, przenoszenie, uzbrajanie
    // alarmu pod skosem — ustanawialo rekordy 60 stopni w MAX OGOLNIE.
    //
    // Drugi warunek: bez kalibracji montazu uklad odniesienia jest przypadkowy,
    // wiec rekordy byly by smieciami. Estymator dziala dalej (widac go
    // w diagnostyce), tylko nic nie zapisujemy.
    if (g_mount.isCalibrated() && g_deviceState.state() == state::DeviceState::Riding) {
        // §16 — bramka predkosci. Przy zywym GPS-ie decyduje predkosc (z
        // histereza i wybiegiem na hamowanie), a bez fixa zostaje dawna regula
        // oparta na bezruchu z IMU.
        const bool recording =
            g_speedGate.isRecording(g_orientation.state().stationary, millis());

        if (recording) g_metrics.update(g_orientation.state());

        // Czas trwania liczymy z tego samego warunku co rekordy: manipulowanie
        // urzadzeniem na parkingu nie jest przejazdem. Przejazd zaczyna sie
        // wiec od przekroczenia progu predkosci, nie od wlaczenia stacyjki.
        g_rideClock.update(recording, millis());
    }
}

/// Odczyt modulu GPS. Wolany w kazdej iteracji petli — zdania przychodza raz
/// na sekunde, ale bufor UART-u ma 256 bajtow, a jedna sekunda ruchu przy
/// 9600 baud to ~600 bajtow. Rzadsze zagladanie gubiloby zdania.
void pumpGps(uint32_t nowMs) {
    // Zasilanie modulu tylko przy wlaczonej stacyjce (§2.5): 32 mA w czuwaniu
    // zabiloby baterie przed rankiem, a stojacy motocykl nie ma predkosci,
    // ktora warto by mierzyc.
    g_gps.setPower(g_deviceState.state() == state::DeviceState::Riding, nowMs);

    // Nowa probka pojawia sie dopiero wraz ze zdaniem RMC — to ono niesie
    // status fixa i predkosc.
    const bool newSample = g_gps.update(nowMs);

    // Nieudana proba mowi wiecej niz cisza: bajty bez zdan to zla predkosc
    // transmisji, a zero bajtow to zly pin albo brak zasilania modulu.
    hal::GpsProbeReport probe;
    if (g_gps.takeProbeReport(probe)) {
        Serial.printf("[gps] proba %lu baud RX=G%d: %lu bajtow, zdania %lu ok / %lu odrzucone\n",
                      static_cast<unsigned long>(probe.baud), probe.rxPin,
                      static_cast<unsigned long>(probe.bytes),
                      static_cast<unsigned long>(probe.validSentences),
                      static_cast<unsigned long>(probe.rejectedSentences));
    }

    // Dobranie (albo utrata) ustawien portu to zdarzenie rzadkie i wazne przy
    // pierwszym uruchomieniu z modulem — niech zostawi slad na porcie USB.
    static bool receivingReported = false;
    if (g_gps.isReceiving() != receivingReported) {
        receivingReported = g_gps.isReceiving();
        if (receivingReported) {
            Serial.printf("[gps] modul odpowiada: %lu baud, RX=G%d\n",
                          static_cast<unsigned long>(g_gps.baud()), g_gps.rxPin());
        } else {
            Serial.println("[gps] modul zamilkl - wracam do szukania ustawien portu");
        }
    }

    if (!newSample) return;

    const motion::SpeedSample sample = g_gps.speed(nowMs);
    if (!sample.valid) return;

    // Bramka predkosci (§16) dostaje kazda probke z fixem — takze zero, bo
    // wlasnie na zerach zamyka rejestracje po wybiegu.
    g_speedGate.updateSpeed(sample.kmh, nowMs);

    // Korekcja przechylu z ustalonego zakretu (§2.4). Filtr ma to wejscie
    // gotowe od poczatku — GPS wlasnie je wypelnia. Predkosc w m/s.
    g_orientation.setSpeedHint(sample.kmh / 3.6f, nowMs);

    // Rekord predkosci zbieramy w tym samym oknie co reszte wynikow: wylacznie
    // przy wlaczonej stacyjce (§17, §18). Kalibracji montazu tu CELOWO nie
    // wymagamy — inaczej niz przechyl, predkosc z GPS nie zalezy od ukladu
    // odniesienia urzadzenia, wiec brak kalibracji nie ma powodu jej blokowac.
    if (g_deviceState.state() == state::DeviceState::Riding) {
        g_metrics.updateSpeed(sample);
    }
}

void refreshDisplay() {
    // Ekran wyboru akcji dopiero po chwili trzymania. Przy krotkim nacisnieciu
    // mignalby na ulamek sekundy i tylko przeszkadzal — akcja i tak wykonuje sie
    // od razu po puszczeniu.
    if (g_button.isPressed() && g_button.heldMs() >= cfg::kHoldPromptDelayMs) {
        g_holdPrompt.draw(g_buffer, g_button);
        return;
    }

    if (g_view == ViewMode::History) {
        // Strona historii: dwa przejazdy na stronie, 1 = najnowszy.
        static char pageLabel[8];
        static char leftNo[4];
        static char rightNo[4];

        const size_t pages = historyPageCount();
        if (g_historyPage >= pages) g_historyPage = 0;
        const size_t leftIndex = g_historyPage * 2;
        const size_t rightIndex = leftIndex + 1;

        std::snprintf(pageLabel, sizeof(pageLabel), "%u/%u",
                      static_cast<unsigned>(g_historyPage + 1), static_cast<unsigned>(pages));
        std::snprintf(leftNo, sizeof(leftNo), "%u", static_cast<unsigned>(leftIndex + 1));
        std::snprintf(rightNo, sizeof(rightNo), "%u", static_cast<unsigned>(rightIndex + 1));

        ui::MainScreenModel model;
        model.overall = g_history.at(leftIndex);
        model.ride = g_history.at(rightIndex);
        model.leftHeader = leftNo;
        model.rightHeader = rightNo;
        model.leftPresent = leftIndex < g_history.count();
        model.rightPresent = rightIndex < g_history.count();
        model.stateLabel = pageLabel;
        model.stateColor = ui::color::kPrimary;
        model.alarmEnabled = g_alarmEnabled;
        model.externalPower = g_power.isExternal();
        model.batteryPercent = M5.Power.getBatteryLevel();
        g_mainScreen.draw(g_buffer, model);
        return;
    }

    if (g_view == ViewMode::Diagnostics) {
        ui::LiveViewModel model;
        model.state = g_orientation.state();
        model.sampleRateHz = g_imu.sampleRateHz();
        model.imuOk = g_imuAvailable;
        model.mountCalibrated = g_mount.isCalibrated();
        g_liveView.draw(g_buffer, model);
        return;
    }

    if (g_view == ViewMode::Hardware) {
        ui::HardwareViewModel model;
        model.scan = &g_i2c;
        model.sampleRateHz = g_imu.sampleRateHz();
        model.batteryPercent = M5.Power.getBatteryLevel();
        model.batteryMillivolts = g_power.batteryMillivolts();
        model.charging = g_power.rawCharging();
        model.externalPower = g_power.isExternal();
        model.maxPulseGapMs = g_power.maxPulseGapMs();
        model.vbusMillivolts = g_power.vbusMillivolts();
        model.stateName = state::stateName(g_deviceState.state());
        model.alarmEnabled = g_alarmEnabled;
        model.alarmArmed = g_alarm.isArmed();
        model.standbySeconds = g_frozenStandbySeconds;
        model.sleepPercent = g_frozenSleepPercent;
        model.awakeMicros = g_frozenAwakeUs;
        model.bufferedDisplay = g_buffer.isBuffered();
        model.freeHeapBytes = ESP.getFreeHeap();
        model.pendingUploads = static_cast<uint32_t>(g_queue.pendingCount(g_history.count()));
        model.lastRideSeq = g_queue.lastSeq();
        model.gpsPowered = g_gps.isPowered();
        model.gpsReceiving = g_gps.isReceiving();
        model.gpsFix = g_gps.hasFix(millis());
        model.gpsBaud = g_gps.baud();
        model.gpsRxPin = g_gps.rxPin();
        model.gpsSatellites = g_gps.satellites();
        model.gpsHdop = g_gps.hdop();
        model.gpsSpeedKmh = g_gps.lastSpeedKmh();
        model.gpsValidSentences = g_gps.validSentences();
        model.gpsRejectedSentences = g_gps.rejectedSentences();
        g_hardwareView.draw(g_buffer, model);
        return;
    }

    ui::MainScreenModel model;
    model.overall = g_metrics.overall();
    model.ride = g_metrics.currentRide();
    model.alarmEnabled = g_alarmEnabled;
    model.mountCalibrated = g_mount.isCalibrated();
    model.externalPower = g_power.isExternal();
    model.batteryPercent = M5.Power.getBatteryLevel();

    static char stateLabel[16];
    const uint32_t untilOff = g_deviceState.msUntilScreenOff(millis());

    if (!g_imuAvailable) {
        model.stateLabel = "IMU BRAK";
        model.stateColor = ui::color::kAlarm;
    } else if (g_deviceState.state() == state::DeviceState::Cooldown) {
        // Odliczanie do zgaszenia ekranu. Przy WYLACZONYM module ochrony
        // odliczanie przeplata sie z ostrzezeniem — po tym, jak wylaczony
        // modul raz przeszedl niezauwazony i "alarm nie dzialal".
        const uint32_t seconds = (untilOff + 999) / 1000;
        if (!g_alarmEnabled && (millis() / 2000) % 2 == 1) {
            std::snprintf(stateLabel, sizeof(stateLabel), "BEZ OCHRONY");
        } else {
            std::snprintf(stateLabel, sizeof(stateLabel), "%lu:%02lu",
                          static_cast<unsigned long>(seconds / 60),
                          static_cast<unsigned long>(seconds % 60));
        }
        model.stateLabel = stateLabel;
        model.stateColor = g_alarmEnabled ? ui::color::kWaiting : ui::color::kAlarm;
    } else if (!g_mount.isCalibrated()) {
        model.stateLabel = "BRAK KAL.";
        model.stateColor = ui::color::kWaiting;
    } else {
        model.stateLabel = state::stateName(g_deviceState.state());
        model.stateColor = ui::color::kRiding;
    }

    g_mainScreen.draw(g_buffer, model);
}

/// Sterowanie glosnikiem wedlug decyzji silnika alarmu. Ton gra do odwolania,
/// wiec wolamy tone() tylko przy zmianie — nie w kazdej iteracji petli.
///
/// Caly tor audio (wzmacniacz) zyje tylko na czas sygnalizacji: zostawiony
/// wlaczony trzeszczal w czuwaniu, bo light sleep przerywal mu strumien I2S.
void driveSiren(const guard::AlarmOutput& out) {
    if (out.signalling && !g_audioActive) {
        M5.Speaker.begin();
        M5.Speaker.setVolume(cfg::kSpeakerVolume);
        g_audioActive = true;
    }

    if (out.sirenOn) {
        if (!g_sirenPlaying || out.freqHz != g_sirenFreqHz) {
            M5.Speaker.tone(out.freqHz);
            g_sirenPlaying = true;
            g_sirenFreqHz = out.freqHz;
        }
    } else if (g_sirenPlaying) {
        M5.Speaker.stop();
        g_sirenPlaying = false;
    }

    if (!out.signalling && g_audioActive) {
        M5.Speaker.stop();
        M5.Speaker.end();
        g_audioActive = false;
        g_sirenPlaying = false;
    }
}

/// Gaszenie i zapalanie ekranu. Podswietlenie zjada wiekszosc pradu, wiec
/// w czuwaniu musi byc naprawde wylaczone, nie tylko wygaszone na czarno.
void setScreenOn(bool on) {
    if (on == g_screenOn) return;
    g_screenOn = on;
    if (on) {
        // Zamrozenie wyniku pomiaru — od tej chwili nie ma juz snu do liczenia.
        const uint32_t seconds =
            g_standbyStartMs == 0 ? 0 : (millis() - g_standbyStartMs) / 1000;
        if (seconds > 0) {
            g_frozenStandbySeconds = seconds;
            g_frozenSleepPercent = static_cast<int>(g_sleepUs / (seconds * 10000ULL));
            g_frozenAwakeUs =
                g_wakeCount == 0 ? 0 : static_cast<uint32_t>(g_awakeUs / g_wakeCount);
        }
        M5.Display.wakeup();
        M5.Display.setBrightness(cfg::kDisplayBrightness);
        g_lastDisplayMs = 0;
    } else {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
        g_sleepUs = 0;
        g_awakeUs = 0;
        g_wakeCount = 0;
        g_lastWakeEndUs = 0;
        g_standbyStartMs = millis();
    }

    // Zielona dioda zasilania swieci non stop — M5Unified zapala ja przy
    // starcie i nigdy nie gasi. Przy baterii 250 mAh to realne obciazenie,
    // a w czuwaniu nikt na nia nie patrzy: motocykl stoi, ekran jest zgaszony.
    // Sterowana bitem LED_EN w PMIC, nie zwyklym GPIO.
    M5.Power.M5pm1.setLedEnLevel(on);
}

/// Po akcji uzytkownika wracamy do normalnej pracy: pomiar czasu probkowania
/// i odswiezania musi ruszyc od nowa, inaczej pierwsza probka dostanie
/// gigantyczne dt z czasu spedzonego na ekranie komunikatu.
void resumeAfterAction() {
    g_lastSampleMicros = 0;
    g_lastDisplayMs = 0;
}

/// Obudzenie zgaszonego ekranu na baterii. Czuwanie jest zawieszone (silnik
/// rozbrojony), a maszyna stanow dostaje krotkie odliczanie — po jego uplywie
/// ekran zgasnie normalna droga i alarm uzbroi sie od nowa.
void wakeScreenOnBattery(uint32_t screenOnMs) {
    g_alarm.disarm();
    driveSiren(guard::AlarmOutput{});
    g_deviceState.wake(millis(), screenOnMs);
    setScreenOn(true);
    resumeAfterAction();
}

/// Klik 1 przy dzwoniacym alarmie: TYLKO wycisza. Modul zostaje wlaczony,
/// po krotkim oknie czuwanie wraca w aktualnej pozycji. Wylaczenie modulu
/// na dobre wymaga drugiego kliku w tym oknie — chroni przed nieswiadomym
/// zostawieniem motocykla bez ochrony po falszywym alarmie.
void silenceAlarm() {
    g_alarm.disarm();
    driveSiren(guard::AlarmOutput{});
    g_deviceState.wake(millis(), cfg::kSilenceScreenMs);
    setScreenOn(true);
    drawMessage("WYCISZONO", "ALARM CZUWA DALEJ", ui::color::kWaiting);
    delay(1500);
    resumeAfterAction();
}

void handleButtons() {
    const bool anyPressed = viewButton().wasPressed() || actionButton().wasPressed();

    if (g_deviceState.state() == state::DeviceState::Triggered && anyPressed) {
        // Gdy alarm wyje, wycisza go DOWOLNY przycisk — w takiej chwili nikt
        // nie zastanawia sie, ktory jest ktory.
        silenceAlarm();
        g_swallowView = true;
        g_swallowAction = true;
    } else if (!g_screenOn && anyPressed) {
        // Przycisk budzacy zgaszony ekran tylko budzi.
        wakeScreenOnBattery(cfg::kWakeScreenMs);
        g_swallowView = true;
        g_swallowAction = true;
    }

    // ── KEY1: widoki ──────────────────────────────────────────────────────
    if (viewButton().wasHold()) {
        if (g_swallowView) {
            g_swallowView = false;
        } else {
            g_view = (g_view == ViewMode::Diagnostics || g_view == ViewMode::Hardware)
                         ? ViewMode::Results
                         : ViewMode::Diagnostics;
            g_lastDisplayMs = 0;
        }
    }

    if (viewButton().wasClicked()) {
        if (g_swallowView) {
            g_swallowView = false;
        } else {
            switch (g_view) {
                case ViewMode::Results:
                    g_view = ViewMode::History;
                    g_historyPage = 0;
                    break;
                case ViewMode::History:
                    ++g_historyPage;
                    if (g_historyPage >= historyPageCount()) g_view = ViewMode::Results;
                    break;
                case ViewMode::Diagnostics:
                    g_view = ViewMode::Hardware;
                    break;
                case ViewMode::Hardware:
                    g_view = ViewMode::Diagnostics;
                    break;
            }
            g_lastDisplayMs = 0;
        }
    }

    // ── KEY2: akcje z progami czasowymi (§22, §23) ────────────────────────
    switch (g_button.update(actionButton().isPressed(), millis())) {
        case input::ButtonAction::Alarm:
            if (g_swallowAction) {
                g_swallowAction = false;
                break;
            }
            toggleAlarm();
            resumeAfterAction();
            break;
        case input::ButtonAction::Track:
            toggleTrack();
            resumeAfterAction();
            break;
        case input::ButtonAction::Reset:
            runResultsReset();
            resumeAfterAction();
            break;
        case input::ButtonAction::Calibration:
            runMountCalibration();
            resumeAfterAction();
            break;
        case input::ButtonAction::Integration:
            runIntegrationScreen();
            resumeAfterAction();
            break;
        case input::ButtonAction::None:
            break;
    }
}

}  // namespace

void setup() {
    auto config = M5.config();
    config.internal_imu = true;
    config.internal_spk = true;
    config.clear_display = true;
    // Wyjscie 5 V na Grove zasila modul GPS, ale wlacza je hal::GpsSource
    // dopiero na czas jazdy (§2.5) — przy starcie zostaje wylaczone, zeby
    // urzadzenie obudzone na parkingu nie karmilo modulu przez caly czas.
    config.output_power = false;
    M5.begin(config);

    M5.Display.setRotation(cfg::kDisplayRotation);
    M5.Display.setBrightness(cfg::kDisplayBrightness);
    M5.Speaker.setVolume(cfg::kSpeakerVolume);

    Serial.begin(115200);

    g_buffer.begin();
    g_imuAvailable = g_imu.begin();
    g_gps.begin(millis());

    // Skan magistrali przed ekranem startowym — jego wynik trafia na ekran
    // jako jedna z linii diagnostycznych.
    g_i2c.run();
    if (g_i2c.allCriticalPresent()) {
        std::snprintf(g_i2cStatus, sizeof(g_i2cStatus), "I2C OK - %u UKLADOW",
                      g_i2c.totalFound());
    } else {
        std::snprintf(g_i2cStatus, sizeof(g_i2cStatus), "I2C - BRAK ISTOTNYCH");
    }

    g_store.begin();

    g_power.begin(millis());
    g_deviceState.begin(g_power.isExternal(), millis());
    restoreState(g_power.isExternal());

#ifdef MMB_BENCH
    // Test stanowiskowy wymaga wlaczonego modulu alarmu — wymuszenie naprawia
    // przy okazji stan zapisany w NVS.
    if (!g_alarmEnabled) {
        g_alarmEnabled = true;
        g_store.saveAlarmEnabled(true);
    }
#endif

#if MMB_RAW_LOGGER
    if (g_logger.begin()) {
        // Boot z zasilaniem = trwajaca sesja jazdy; zdarzenie RideStarted nie
        // padnie, wiec sesje zapisu otwieramy tutaj.
        if (g_deviceState.state() == state::DeviceState::Riding) {
            g_logger.startSession(millis());
        }
    }
#endif

    runSplash();

    Serial.printf("\n=== %s %s ===\n", cfg::kDeviceName, cfg::kFirmwareVersion);
    g_i2c.printTo(Serial);
    Serial.printf("IMU: %s | %s | bufor: %s | alarm: %s | slad: %s\n",
                  g_imuAvailable ? "OK" : "BRAK", g_storageStatus,
                  g_buffer.isBuffered() ? "PSRAM" : "bezposredni",
                  g_alarmEnabled ? "WL" : "WYL", g_trackEnabled ? "WL" : "WYL");
    Serial.printf("Bateria: %d%% (%d mV), zasilanie: %s (impuls ladowania: %s)\n",
                  M5.Power.getBatteryLevel(), g_power.batteryMillivolts(),
                  g_power.isExternal() ? "ZEWNETRZNE" : "bateria",
                  g_power.rawCharging() ? "tak" : "nie");
#if MMB_RAW_LOGGER
    Serial.printf("Rejestrator: %s, sesja: %s (komendy: L, D<nr>, X)\n",
                  g_logger.isMounted() ? "OK" : "BLAD PARTYCJI",
                  g_logger.isLogging() ? "AKTYWNA" : "nie");
#endif
    Serial.printf("RAM wolne: %u B, PSRAM wolne: %u B\n",
                  static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getFreePsram()));

    // Stan integracji przy kazdym starcie: to jedyne ustawienie, ktore
    // uzytkownik wpisuje recznie, wiec ma byc widoczne bez pytania o nie.
    printIntegrationStatus(Serial);
    printConfigHelp(Serial);

    // Nazwa i haslo sieci konfiguracyjnej sa wyprowadzone z device_id, wiec
    // znane juz przy starcie — wypisanie ich tutaj oszczedza wchodzenie
    // w ekran INTEGRACJA tylko po to, zeby je odczytac.
    char apSsid[16];
    char apPassword[16];
    telemetry::portalSsid(hal::deviceId(), apSsid, sizeof(apSsid));
    telemetry::portalPassword(hal::deviceId(), apPassword, sizeof(apPassword));
    Serial.printf("[konfig] device_id: %s | konfiguracja z telefonu: siec \"%s\", haslo \"%s\"\n",
                  hal::deviceId(), apSsid, apPassword);
}

/// Reakcja na przejscia maszyny stanow (§17, §21, §26).
void handleStateEvent(state::DeviceEvent event) {
    switch (event) {
        case state::DeviceEvent::RideStarted:
            // Stacyjka wlaczona: rozbrojenie alarmu (§21), nowa sesja.
            // Siatka bezpieczenstwa: jesli poprzedni przejazd nie zostal
            // zarchiwizowany (restart na baterii), robimy to teraz.
            archiveCurrentRide();
            g_alarm.disarm();
            driveSiren(guard::AlarmOutput{});
            g_metrics.startNewRide();
            g_rideClock.reset();
            g_speedGate.reset();
            g_rideArchived = false;
#if MMB_RAW_LOGGER
            g_logger.startSession(millis());
#endif
            saveResultsIfDirty(true);
            g_orientation.resetAngles();
            setScreenOn(true);
            resumeAfterAction();
            break;

        case state::DeviceEvent::PowerLost:
            // Stacyjka zgaszona = przejazd skonczony -> do historii.
            // Bateria daje nam cale minuty na spokojny zapis — zadnego wyscigu
            // z zanikajacym napieciem.
            saveResultsIfDirty(true);
            archiveCurrentRide();
#if MMB_RAW_LOGGER
            g_logger.stopSession();
#endif
            break;

        case state::DeviceEvent::ScreenOff:
            // REGULA NACZELNA scenariusza: ekran zgaszony na baterii + modul
            // wlaczony => silnik uzbrojony. Kazda droga do zgaszonego ekranu
            // przechodzi tedy: koniec odliczania, timeout po obudzeniu,
            // okno po wyciszeniu, wlaczenie modulu przyciskiem.
            saveResultsIfDirty(true);
            setScreenOn(false);
            if (g_alarmEnabled && g_haveSampleEver) {
                // Pozycja odniesienia = pozycja w momencie uzbrojenia, wiec
                // motocykl na bocznej stopce nie wywola wlasnego alarmu.
                g_alarm.arm(g_lastSample.accelG, millis());
            }
            break;

        case state::DeviceEvent::MotionDetected:
            // Ekran budzi sie z komunikatem RUCH! — sygnalizacja ma informowac
            // osobe poruszajaca motocykl, ze zostala zauwazona (§20).
            setScreenOn(true);
            break;

        case state::DeviceEvent::AlarmCleared:
            driveSiren(guard::AlarmOutput{});
            break;

        case state::DeviceEvent::None:
            break;
    }
}

void loop() {
    M5.update();

    const uint32_t nowMs = millis();

    pumpImu();
    pumpGps(nowMs);
    handleButtons();

    g_power.update(nowMs);

    // §16 — TWARDA GWARANCJA: wylaczony modul oznacza zero czuwania i zero
    // dzwieku, niezaleznie od tego, jaka sciezka doprowadzila do biezacego
    // stanu. Sprawdzane w kazdej iteracji, nie tylko przy zmianie stanu.
    if (!g_alarmEnabled && g_alarm.isArmed()) {
        g_alarm.disarm();
        driveSiren(guard::AlarmOutput{});
    }

    const state::DeviceState deviceState = g_deviceState.state();

    // REGULA NACZELNA jako funkcja stanu, nie zdarzenia. Przejscie
    // Idle -> Armed (wlaczenie modulu przy juz zgaszonym ekranie) nie generuje
    // zdarzenia ScreenOff, wiec bez tego silnik zostawalby nieuzbrojony mimo
    // napisu ALARM na ekranie.
    if (g_alarmEnabled && !g_alarm.isArmed() && !g_screenOn && g_haveSampleEver &&
        deviceState == state::DeviceState::Armed) {
        g_alarm.arm(g_lastSample.accelG, nowMs);
    }

    // Silnik alarmu pracuje tylko w stanach czuwania i sygnalizacji.
    guard::AlarmOutput alarmOut;
    if (g_alarm.isArmed() && (deviceState == state::DeviceState::Armed ||
                              deviceState == state::DeviceState::Triggered)) {
        alarmOut = g_alarm.update(g_haveNewSample ? &g_lastSample : nullptr, nowMs);
        g_haveNewSample = false;
        driveSiren(alarmOut);

        // Sygnalizacja dobiegla konca -> wracamy do cichego czuwania.
        if (deviceState == state::DeviceState::Triggered && !alarmOut.signalling) {
            g_deviceState.rearm();
            setScreenOn(false);
        }
    }

#ifdef MMB_BENCH
    const bool externalPower = effectiveExternalPower();
#else
    const bool externalPower = g_power.isExternal();
#endif
    handleStateEvent(g_deviceState.update(externalPower, g_alarmEnabled,
                                          alarmOut.violation, nowMs));

    // ── Wysylka wynikow na strone ──────────────────────────────────────────
    // Radio wolno wlaczyc TYLKO w oknie po zgaszeniu stacyjki. Wtedy przejazd
    // jest skonczony (nie ma czego mierzyc, a wysylka blokuje petle na
    // kilkanascie sekund), motocykl stoi w garazu w zasiegu sieci domowej,
    // a urzadzenie jeszcze nie zasnelo. W czuwaniu nie budzimy sie po to,
    // zeby wysylac — dwa dni czuwania sa wazniejsze.
    const bool radioAllowed = g_deviceState.state() == state::DeviceState::Cooldown;
    if (g_scheduler.shouldAttempt(g_integration.isComplete(),
                                  g_queue.pendingCount(g_history.count()) > 0, radioAllowed,
                                  nowMs)) {
        runScheduledUpload();
        resumeAfterAction();
    }

#ifdef MMB_BENCH
    static uint32_t benchLastMs = 0;
    if (nowMs - benchLastMs >= 1000) {
        benchLastMs = nowMs;
        Serial.printf("[bench] st=%-8s scr=%d armE=%d viol=%u imu=%.0fHz "
                      "tilt=%.1f mag=%.2f pwr=%d sig=%d\n",
                      state::stateName(g_deviceState.state()), g_screenOn ? 1 : 0,
                      g_alarm.isArmed() ? 1 : 0, g_alarm.violationCount(),
                      static_cast<double>(g_imu.sampleRateHz()),
                      static_cast<double>(g_alarm.debugTiltDeg()),
                      static_cast<double>(g_alarm.debugMagDeviationG()),
                      externalPower ? 1 : 0, g_audioActive ? 1 : 0);
    }
#endif

    saveResultsIfDirty(false);

    pumpSerial(Serial);

    if (g_screenOn && nowMs - g_lastDisplayMs >= cfg::kDisplayRefreshMs) {
        g_lastDisplayMs = nowMs;
        refreshDisplay();
    }

    // Light sleep miedzy probkami IMU (~25 Hz czuwania). Ten stan istnieje
    // wylacznie na baterii, wiec utrata USB nie jest problemem. Przyciski
    // (GPIO 11/12, aktywne w stanie niskim) budza natychmiast.
    if (!g_screenOn && g_deviceState.maySleep() && !g_audioActive) {
        // Przy wylaczonym alarmie nie ma czego probkowac — budzimy sie tylko
        // po to, zeby sprawdzic, czy nie wrocilo zasilanie.
        const uint32_t wakeIntervalMs = (deviceState == state::DeviceState::Armed)
                                            ? cfg::kArmedSampleIntervalMs
                                            : cfg::kIdleWakeIntervalMs;
#if defined(MMB_BENCH) && !defined(MMB_BENCH_SLEEP)
        // Faza 1 testu stanowiskowego: bez light sleep, zeby serial zyl.
        delay(wakeIntervalMs);
#else
        // Domeny RTC nie sa nam do niczego potrzebne — niech spia razem z reszta.
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
        esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);

        gpio_wakeup_enable(GPIO_NUM_11, GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(GPIO_NUM_12, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_sleep_enable_timer_wakeup(wakeIntervalMs * 1000ULL);

        const int64_t before = esp_timer_get_time();
        // Czas aktywny to odcinek od wyjscia z poprzedniego snu do wejscia
        // w kolejny — czyli dokladnie jedna iteracja petli glownej.
        if (g_lastWakeEndUs != 0) {
            g_awakeUs += static_cast<uint64_t>(before - g_lastWakeEndUs);
            ++g_wakeCount;
        }
        esp_light_sleep_start();
        const int64_t after = esp_timer_get_time();
        g_sleepUs += static_cast<uint64_t>(after - before);
        g_lastWakeEndUs = after;
#endif
    }
}
