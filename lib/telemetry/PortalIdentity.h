// Motusy Moto Box — nazwa i haslo punktu dostepowego konfiguracji.
//
// Urzadzenie na czas konfiguracji stawia wlasna siec WiFi, do ktorej wlasciciel
// laczy sie telefonem. Nazwa i haslo musza byc POWTARZALNE: konfiguracje robi
// sie raz, a poprawia po miesiacach, i wtedy telefon ma sie polaczyc sam,
// z zapamietanej sieci — nie z kartki, ktora dawno zginela.
//
// Dlatego oba wyprowadzamy z device_id (fabryczny MAC), a nie losujemy.
//
// SWIADOMY KOMPROMIS: device_id jest publiczny (widac go w eterze), a firmware
// jest otwarty, wiec haslo tej sieci da sie policzyc. Chroni nas okno czasowe,
// nie sekret — punkt dostepowy zyje tylko wtedy, gdy wlasciciel stoi przy
// motocyklu z otwartym ekranem INTEGRACJA, i gasnie razem z nim. Sieci bez
// hasla celowo nie stawiamy: tam kazdy przechodzien podmienilby token jednym
// kliknieciem, bez liczenia czegokolwiek.
//
// Czyste C++ bez zaleznosci od sprzetu.

#pragma once

#include <cstddef>

namespace telemetry {

/// "MOTOBOX-" + cztery ostatnie znaki device_id, wielkimi literami.
/// Cztery znaki wystarcza, zeby odroznic swoje urzadzenie od sasiada.
/// Bufor: 16 bajtow z zapasem.
void portalSsid(const char* deviceId, char* out, size_t outSize);

/// Haslo wyprowadzone z device_id — dziesiec znakow z alfabetu bez par
/// mylacych sie na ekranie (bez O i 0, bez I i 1). Przepisuje sie je
/// z ekranu 240x135 recznie, wiec czytelnosc jest wazniejsza niz entropia.
/// Bufor: 16 bajtow z zapasem.
void portalPassword(const char* deviceId, char* out, size_t outSize);

/// Ile znakow ma haslo. WPA2 wymaga co najmniej osmiu.
constexpr size_t kPortalPasswordLen = 10;

}  // namespace telemetry
