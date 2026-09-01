#include "DeviceId.h"

#include <Arduino.h>

#include <cstdio>

namespace hal {

const char* deviceId() {
    static char id[13] = {};

    if (id[0] == '\0') {
        // ESP.getEfuseMac() oddaje MAC w kolejnosci odwroconej wzgledem tej,
        // ktora widac na etykiecie i w nazwie sieci. Skladamy bajt po bajcie,
        // zeby identyfikator dalo sie porownac z tym, co pokazuje router.
        const uint64_t mac = ESP.getEfuseMac();
        std::snprintf(id, sizeof(id), "%02x%02x%02x%02x%02x%02x",
                      static_cast<unsigned>((mac >> 0) & 0xFF),
                      static_cast<unsigned>((mac >> 8) & 0xFF),
                      static_cast<unsigned>((mac >> 16) & 0xFF),
                      static_cast<unsigned>((mac >> 24) & 0xFF),
                      static_cast<unsigned>((mac >> 32) & 0xFF),
                      static_cast<unsigned>((mac >> 40) & 0xFF));
    }
    return id;
}

}  // namespace hal
