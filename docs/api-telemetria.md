# Motusy Moto Box — kontrakt API telemetrii

Specyfikacja dla serwera `motobix.motusy.top` (Laravel). Opisuje to, co
urządzenie faktycznie wysyła — format jest zaimplementowany w
`lib/telemetry/TelemetryJson.cpp` i obłożony testami, które sprawdzają
dosłowną treść JSON-a. Zmiana kształtu przesyłki psuje te testy; to celowe,
bo taka zmiana wymaga ruszenia także drugiej strony.

Wersja: 1 (2026-08-29).

---

## 1. Zasady

**Urządzenie jest offline'owe i nieufne wobec sieci.** Wysyła, gdy może, i
zakłada, że przesyłka mogła nie dojść albo że nie dotarło potwierdzenie.

**Numer przejazdu `seq`** rośnie monotonicznie przez całe życie urządzenia i
**nie jest zerowany przez reset wyników**. Para `(device_id, seq)` jest kluczem
unikalnym po stronie API. Powtórna wysyłka tego samego przejazdu nie może
tworzyć duplikatu — ma trafić w istniejący wpis.

**Kolejność wyników wynika z `seq`, nie z czasu.** Urządzenie nie ma zegara
czasu rzeczywistego. Do momentu montażu modułu GPS `recorded_at` jest `null`,
a data na stronie po prostu się nie pokazuje.

**Kasowania nie ma.** Wszystko, co trafiło do historii w urządzeniu, ma trafić
na serwer. Usuwanie odbywa się wyłącznie po zalogowaniu na stronie i wyłącznie
jako soft-delete — inaczej skasowany przejazd wróciłby przy następnej wysyłce.

---

## 2. Uwierzytelnianie

Token konta, uzyskiwany po rejestracji na stronie i przepisywany do urządzenia
raz, przy konfiguracji WiFi.

```
Authorization: Bearer <token konta>
Content-Type: application/json
User-Agent: MotusyMotoBox/<wersja firmware>
```

`device_id` w treści to fabryczny identyfikator układu (12 znaków hex z eFuse
MAC). Służy **wyłącznie do rozróżnienia urządzeń w obrębie jednego konta** —
jest publiczny i nie może pełnić roli sekretu.

Urządzenie nieznane serwerowi ma zostać przypisane do konta właściciela tokena
przy pierwszej udanej wysyłce (bez osobnego kroku rejestracji urządzenia).

**Transport: wyłącznie HTTPS.** Urządzenie ma wbudowany certyfikat główny.
Zmiana wystawcy certyfikatu na serwerze unieruchomi wszystkie urządzenia
w terenie — nie da się tego naprawić bez podłączenia kabla do każdego z nich.

---

## 3. `POST /api/v1/rides`

Jedna przesyłka zawiera **od 0 do 10 przejazdów**, uporządkowanych rosnąco po
`seq`, i nie przekracza 4 kB. Urządzenie wysyła wszystkie zaległe przejazdy
naraz — nawiązanie połączenia TLS jest najdroższą częścią operacji, więc nie
opłaca się dzielić na pojedyncze żądania.

### Treść żądania

```json
{
  "device_id": "a1b2c3d4e5f6",
  "fw": "1.0.0",
  "calibrated": true,
  "rides": [
    {
      "seq": 7,
      "recorded_at": null,
      "duration_s": 1832,
      "lean_left_deg": 42.0,
      "lean_right_deg": 38.0,
      "accel_g": 0.75,
      "brake_g": 0.50,
      "speed_kmh": null
    }
  ]
}
```

| Pole | Typ | Opis |
|---|---|---|
| `device_id` | string, 12 hex | identyfikator układu, stały przez całe życie urządzenia |
| `fw` | string | wersja firmware — do diagnostyki zgłoszeń „dziwne wyniki" |
| `calibrated` | bool | czy urządzenie ma kalibrację montażu; bez niej pomiary nie są zbierane |
| `rides[].seq` | int > 0 | numer przejazdu w urządzeniu; klucz idempotencji |
| `rides[].recorded_at` | int lub null | unix timestamp końca przejazdu; `null` dopóki nie ma GPS |
| `rides[].duration_s` | int | czas trwania przejazdu w sekundach |
| `rides[].lean_left_deg` | float, 1 miejsce | maksymalny przechył w lewo |
| `rides[].lean_right_deg` | float, 1 miejsce | maksymalny przechył w prawo |
| `rides[].accel_g` | float, 2 miejsca | maksymalne przyspieszenie |
| `rides[].brake_g` | float, 2 miejsca | maksymalne hamowanie |
| `rides[].speed_kmh` | float lub null | prędkość maksymalna; `null` gdy brak GPS |

`null` w `speed_kmh` znaczy „urządzenie nie umiało tego zmierzyć", a nie
„zero" — dokładnie jak na ekranie, gdzie bez GPS pokazuje się `---`.
Interfejs strony powinien to rozróżniać.

### Odpowiedź

```json
{ "accepted_through": 7 }
```

Numer **ostatniego przejazdu przyjętego i zapisanego**. Urządzenie zapamiętuje
tę jedną liczbę jako znacznik „wysłane do numeru N" i przy następnej okazji
wysyła tylko to, co ma numer wyższy.

Konsekwencje dla implementacji:

- przejazdy przetwarzać **po kolei i w transakcji**; `accepted_through` ma być
  numerem ostatniego przejazdu zapisanego **bez przerwy w ciągu** — jeśli
  przejazd nr 5 się nie zapisał, a 6 i 7 tak, w odpowiedzi ma być `4`,
- pominięcie tego pola albo `null` znaczy dla urządzenia „nic nie przyjęto";
  wszystkie przejazdy zostają w kolejce do następnej próby,
- przejazd już znany (ten sam `device_id` i `seq`) jest **sukcesem**, nie
  konfliktem — ma podnieść `accepted_through`, a dane nadpisać przez upsert.

### Kody odpowiedzi

| Kod | Znaczenie | Reakcja urządzenia |
|---|---|---|
| 200 | przyjęto (w całości lub częściowo) | przesuwa znacznik na `accepted_through` |
| 401 / 403 | token nieprawidłowy lub odwołany | **przestaje próbować**, pokazuje błąd na ekranie do czasu zmiany konfiguracji |
| 422 | przesyłka niepoprawna | traktuje jak awarię serwera, ponawia z opóźnieniem |
| 429 | za dużo żądań | ponawia z opóźnieniem |
| 5xx, brak odpowiedzi | awaria | ponawia z rosnącym opóźnieniem |

Reakcja na 401 jest istotna: bez niej urządzenie z błędnie przepisanym tokenem
budziłoby radio co kilka minut aż do rozładowania baterii.

---

## 4. `GET /api/v1/ping`

Sprawdzenie konfiguracji zaraz po jej zapisaniu, żeby użytkownik od razu
zobaczył na ekranie urządzenia, czy token został przepisany poprawnie.

Ten sam nagłówek `Authorization`. Odpowiedź 200 z dowolną treścią oznacza
„token dobry"; 401 oznacza „token zły". Nic więcej nie jest potrzebne.

---

## 5. Po stronie bazy

- unikalny indeks na `(device_id, seq)`,
- `recorded_at` musi być **nullable**,
- `speed_kmh` musi być **nullable** (brak GPS to nie zero),
- sortowanie historii po `seq` malejąco, nie po dacie,
- kasowanie wyłącznie jako soft-delete,
- urządzeń w koncie może być wiele; widok powinien pozwalać je rozróżnić
  (na razie po `device_id`, docelowo po nazwie nadanej na stronie).

---

## 6. Co zmieni moduł GPS

Kształt przesyłki się nie zmienia — wypełnią się pola, które dziś są `null`:
`recorded_at` (czas z GPS) i `speed_kmh`. Dojdą prawdopodobnie dane trasy
(dystans, punkt startu). Warto od razu zaprojektować tabelę tak, żeby
dołożenie pól nie wymagało wersjonowania API.
