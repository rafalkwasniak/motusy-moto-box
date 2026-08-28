// Motusy Moto Box — skan wewnetrznej magistrali I2C.
//
// StickS3 trzyma IMU, PMIC i kodek audio na jednej magistrali (SDA=47, SCL=48).
// Skan odpowiada na pytania, ktorych nie da sie rozstrzygnac z dokumentacji:
//
//   - czy BMI270 siedzi pod 0x68 czy 0x69 (zrodla podaja oba),
//   - czy urzadzenie ma RTC PCF8563 pod 0x51 — pozycja V3 w architekturze §1.1,
//     gdzie sklep twierdzi "brak RTC", a dokumentacja spolecznosci "jest",
//   - czy na magistrali nie ma czegos, o czym nie wiemy.

#pragma once

#include <M5Unified.h>

#include <cstddef>
#include <cstdint>

namespace hal {

struct I2cDevice {
    uint8_t address;
    const char* name;
    /// Czy brak tego ukladu oznacza, ze urzadzenie nie zadziala.
    bool critical;
    bool present;
};

class I2cScan {
public:
    /// Adresy niebedace zadnym ze znanych ukladow. Wiecej nie spodziewamy sie
    /// zobaczyc bez podlaczonego modulu na Grove.
    static constexpr size_t kMaxUnknown = 8;

    void run();

    const I2cDevice* devices() const { return devices_; }
    size_t deviceCount() const;

    const uint8_t* unknown() const { return unknown_; }
    size_t unknownCount() const { return unknownCount_; }

    /// Ile ukladow w sumie odpowiedzialo na magistrali.
    uint8_t totalFound() const { return totalFound_; }

    /// Czy wszystkie uklady oznaczone jako krytyczne sa obecne.
    bool allCriticalPresent() const;

    /// Pozycja V3 z architektury §1.1.
    bool hasRtc() const;

    /// IMU moze siedziec pod 0x68 albo 0x69 — liczy sie, ze odpowiedzial ktorykolwiek.
    bool hasImu() const;

    /// Adres, pod ktorym faktycznie odpowiedzial BMI270. Zero gdy zadnego nie ma.
    uint8_t imuAddress() const;

    /// Czytelny raport na port szeregowy.
    void printTo(Print& out) const;

private:
    I2cDevice devices_[5] = {
        {0x18, "ES8311", false, false},   // kodek audio
        {0x51, "PCF8563", false, false},  // RTC — obecnosc niepotwierdzona (V3)
        {0x68, "BMI270", false, false},   // IMU, adres podstawowy
        {0x69, "BMI270-B", false, false}, // IMU, adres alternatywny
        {0x6E, "M5PM1", true, false},     // PMIC
    };

    uint8_t unknown_[kMaxUnknown] = {};
    size_t unknownCount_ = 0;
    uint8_t totalFound_ = 0;
};

}  // namespace hal
