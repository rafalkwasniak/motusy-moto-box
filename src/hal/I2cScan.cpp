#include "I2cScan.h"

namespace hal {
namespace {
/// Zakres adresow 7-bitowych zgodny ze standardem I2C; 0x00-0x07 i 0x78-0x7F
/// sa zarezerwowane.
constexpr uint8_t kFirstAddress = 0x08;
constexpr uint8_t kLastAddress = 0x77;
}  // namespace

size_t I2cScan::deviceCount() const {
    return sizeof(devices_) / sizeof(devices_[0]);
}

void I2cScan::run() {
    unknownCount_ = 0;
    totalFound_ = 0;
    for (size_t i = 0; i < deviceCount(); ++i) devices_[i].present = false;

    bool responded[128] = {};
    M5.In_I2C.scanID(responded);

    for (uint8_t address = kFirstAddress; address <= kLastAddress; ++address) {
        if (!responded[address]) continue;
        ++totalFound_;

        bool recognised = false;
        for (size_t i = 0; i < deviceCount(); ++i) {
            if (devices_[i].address != address) continue;
            devices_[i].present = true;
            recognised = true;
            break;
        }

        if (!recognised && unknownCount_ < kMaxUnknown) {
            unknown_[unknownCount_++] = address;
        }
    }
}

bool I2cScan::hasImu() const {
    return devices_[2].present || devices_[3].present;
}

uint8_t I2cScan::imuAddress() const {
    if (devices_[2].present) return devices_[2].address;
    if (devices_[3].present) return devices_[3].address;
    return 0;
}

bool I2cScan::hasRtc() const {
    return devices_[1].present;
}

bool I2cScan::allCriticalPresent() const {
    if (!hasImu()) return false;
    for (size_t i = 0; i < deviceCount(); ++i) {
        if (devices_[i].critical && !devices_[i].present) return false;
    }
    return true;
}

void I2cScan::printTo(Print& out) const {
    out.printf("I2C (SDA=47 SCL=48): %u ukladow\n", totalFound_);
    for (size_t i = 0; i < deviceCount(); ++i) {
        const I2cDevice& device = devices_[i];
        out.printf("  0x%02X  %-9s %s\n", device.address, device.name,
                   device.present ? "OK" : "brak");
    }
    for (size_t i = 0; i < unknownCount_; ++i) {
        out.printf("  0x%02X  %-9s nierozpoznany\n", unknown_[i], "?");
    }
    out.printf("  RTC PCF8563: %s\n", hasRtc() ? "OBECNY" : "BRAK");
}

}  // namespace hal
