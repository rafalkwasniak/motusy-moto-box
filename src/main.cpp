// Motusy Moto Box — punkt wejscia firmware.
//
// Zaimplementowane:
//   E2  start urzadzenia, ekran startowy, BMI270, estymacja orientacji, dwa widoki
//   E5  pamiec nieulotna: wyniki, kalibracja, stan alarmu
//   E7  przycisk — trzy progi czasowe (§22, §23)
//   E6  maszyna stanow zasilania — wykrywanie stacyjki, gaszenie ekranu
//   E8  alarm: detekcja ruchu, sygnalizacja glosnikiem, light sleep w czuwaniu
//
// Pozostaje:
//   E3  rejestrator surowych danych do CSV (czeka na GPS i motocykl)
//   GPS parser NMEA, predkosc do filtru i do rekordow

#include <Arduino.h>
#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "AlarmEngine.h"
#include "ButtonFsm.h"
#include "DeviceStateMachine.h"
#include "Orientation.h"
#include "RideMetrics.h"
#include "config.h"
#include "hal/I2cScan.h"
#include "hal/ImuSource.h"
#include "hal/PowerSource.h"
#include "hal/Store.h"
#if MMB_RAW_LOGGER
#include "log/RawLogger.h"
#endif
#include "ui/HardwareView.h"
#include "ui/HoldPrompt.h"
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
/// Czy biezacy przejazd trafil juz do historii — patrz archiveCurrentRide().
bool g_rideArchived = false;

hal::ImuSource g_imu;
hal::Store g_store;
hal::I2cScan g_i2c;

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

/// KEY2 klik: wyniki -> strony historii (po 2 przejazdy) -> wyniki.
/// KEY2 przytrzymanie: widoki serwisowe (diagnostyka/sprzet) — potrzebne przy
/// montazu i strojeniu, ale ukryte przed codziennym klikaniem.
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

/// Progi przycisku pochodza wprost z §22 specyfikacji.
input::ButtonFsmConfig makeButtonConfig() {
    input::ButtonFsmConfig config;
    config.mediumHoldMs = cfg::kButtonResetHoldMs;
    config.longHoldMs = cfg::kButtonCalibrationHoldMs;
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
    // Antydatowanie: pelen lancuch detekcji zaniku to okno tetna + potwierdzenie.
    config.detectionLatencyMs = cfg::kChargePulseHoldMs + cfg::kPowerLossConfirmMs;
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
/// Klik, ktory obudzil zgaszony ekran, nie wykonuje swojej normalnej funkcji.
bool g_swallowClick = false;

bool g_imuAvailable = false;
bool g_alarmEnabled = true;
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
    if (!g_history.push(g_metrics.currentRide())) return;  // pusty przejazd
    g_rideArchived = true;
    g_store.saveHistory(g_history);
    g_store.saveResults(g_metrics.overall(), g_metrics.currentRide(), g_rideArchived);
}

/// Zapis wynikow wedlug strategii z architektury §6.2: okresowo i tylko gdy
/// cokolwiek sie zmienilo. `force` pomija odstep czasowy — uzywane przy akcjach
/// uzytkownika i (docelowo) przy zaniku zasilania.
void saveResultsIfDirty(bool force) {
    if (!g_metrics.dirty()) return;

    const uint32_t nowMs = millis();
    if (!force && nowMs - g_lastAutosaveMs < cfg::kAutosaveIntervalMs) return;

    if (g_store.saveResults(g_metrics.overall(), g_metrics.currentRide(), g_rideArchived)) {
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
    const bool saved =
        g_store.saveResults(g_metrics.overall(), g_metrics.currentRide(), g_rideArchived);
    if (saved) g_metrics.clearDirty();

    drawMessage("POMIARY WYZEROWANE", saved ? "" : "BLAD ZAPISU",
                saved ? ui::color::kRiding : ui::color::kAlarm);
    delay(1500);
}

/// §22.1 — przelaczenie modulu alarmowego.
void toggleAlarm() {
    g_alarmEnabled = !g_alarmEnabled;
    g_store.saveAlarmEnabled(g_alarmEnabled);

    drawMessage(g_alarmEnabled ? "ALARM WLACZONY" : "ALARM WYLACZONY", "",
                g_alarmEnabled ? ui::color::kAlarm : ui::color::kMuted);
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
    g_history = state.history;
    g_rideArchived = state.rideArchived;

    if (state.mountCalibrated) {
        g_mount.restore(state.mountRotation);
    }
    g_orientation.setMount(g_mount);

    if (externalPowerAtBoot) {
        // Nowa sesja. Jesli poprzedni przejazd nie zdazyl trafic do historii
        // (urzadzenie padlo w trakcie), ratujemy go teraz — przed wyzerowaniem.
        g_metrics.restore(state.overall, state.ride);
        archiveCurrentRide();
        g_metrics.startNewRide();
        g_metrics.clearDirty();
        g_rideArchived = false;
    } else {
        // Restart na baterii: przejazd trwa dalej (§25).
        g_metrics.restore(state.overall, state.ride);
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
        g_metrics.update(g_orientation.state());
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
        // TODO(GPS): wspolna flaga z ekranem glownym.
        model.speedAvailable = false;
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
        model.stateName = state::stateName(g_deviceState.state());
        model.alarmEnabled = g_alarmEnabled;
        model.alarmArmed = g_alarm.isArmed();
        model.bufferedDisplay = g_buffer.isBuffered();
        model.freeHeapBytes = ESP.getFreeHeap();
        model.freePsramBytes = ESP.getFreePsram();
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
    // TODO(GPS): ustawic na true, gdy parser NMEA zaraportuje fix. Do tego czasu
    // wiersz predkosci pokazuje "---" zamiast mylacego zera.
    model.speedAvailable = false;

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
        M5.Display.wakeup();
        M5.Display.setBrightness(cfg::kDisplayBrightness);
        g_lastDisplayMs = 0;
    } else {
        M5.Display.setBrightness(0);
        M5.Display.sleep();
    }
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
    // Dowolny przycisk budzi zgaszony ekran — i zostaje "polkniety",
    // czyli nie wykonuje swojej normalnej funkcji.
    if (!g_screenOn && (M5.BtnA.wasPressed() || M5.BtnB.wasPressed())) {
        wakeScreenOnBattery(cfg::kWakeScreenMs);
        g_swallowClick = true;
    }

    // KEY2 przytrzymanie: wejscie/wyjscie z widokow serwisowych.
    if (M5.BtnB.wasHold()) {
        if (g_swallowClick) {
            g_swallowClick = false;
        } else {
            g_view = (g_view == ViewMode::Diagnostics || g_view == ViewMode::Hardware)
                         ? ViewMode::Results
                         : ViewMode::Diagnostics;
            g_lastDisplayMs = 0;
        }
    }

    // KEY2 klik: kartkowanie historii; w trybie serwisowym — zmiana widoku.
    if (M5.BtnB.wasClicked()) {
        if (g_swallowClick) {
            g_swallowClick = false;
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

    switch (g_button.update(M5.BtnA.isPressed(), millis())) {
        case input::ButtonAction::ShortPress:
            if (g_swallowClick) {
                g_swallowClick = false;
                break;
            }
            if (g_deviceState.state() == state::DeviceState::Triggered) {
                silenceAlarm();
            } else {
                toggleAlarm();
                resumeAfterAction();
            }
            break;
        case input::ButtonAction::MediumHold:
            runResultsReset();
            resumeAfterAction();
            break;
        case input::ButtonAction::LongHold:
            runMountCalibration();
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
    // Wyjscie 5 V na Grove wlaczymy dopiero razem z modulem GPS — teraz
    // niepotrzebnie obciazaloby baterie.
    config.output_power = false;
    M5.begin(config);

    M5.Display.setRotation(cfg::kDisplayRotation);
    M5.Display.setBrightness(cfg::kDisplayBrightness);
    M5.Speaker.setVolume(cfg::kSpeakerVolume);

    Serial.begin(115200);

    g_buffer.begin();
    g_imuAvailable = g_imu.begin();

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
    Serial.printf("IMU: %s | %s | bufor: %s | alarm: %s\n",
                  g_imuAvailable ? "OK" : "BRAK", g_storageStatus,
                  g_buffer.isBuffered() ? "PSRAM" : "bezposredni",
                  g_alarmEnabled ? "WL" : "WYL");
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

#if MMB_RAW_LOGGER
    g_logger.handleSerial(Serial);
#endif

    if (g_screenOn && nowMs - g_lastDisplayMs >= cfg::kDisplayRefreshMs) {
        g_lastDisplayMs = nowMs;
        refreshDisplay();
    }

    // Light sleep miedzy probkami IMU (~25 Hz czuwania). Ten stan istnieje
    // wylacznie na baterii, wiec utrata USB nie jest problemem. Przyciski
    // (GPIO 11/12, aktywne w stanie niskim) budza natychmiast.
    if (!g_screenOn && g_deviceState.maySleep() && !g_audioActive) {
#if defined(MMB_BENCH) && !defined(MMB_BENCH_SLEEP)
        // Faza 1 testu stanowiskowego: bez light sleep, zeby serial zyl.
        delay(cfg::kArmedSampleIntervalMs);
#else
        gpio_wakeup_enable(GPIO_NUM_11, GPIO_INTR_LOW_LEVEL);
        gpio_wakeup_enable(GPIO_NUM_12, GPIO_INTR_LOW_LEVEL);
        esp_sleep_enable_gpio_wakeup();
        esp_sleep_enable_timer_wakeup(cfg::kArmedSampleIntervalMs * 1000ULL);
        esp_light_sleep_start();
#endif
    }
}
