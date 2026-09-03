# Motusy Moto Box — kontrakt API telemetrii

Specyfikacja dla serwera `motusy.top` (Laravel). Opisuje to, co
urządzenie faktycznie wysyła — format jest zaimplementowany w
`lib/telemetry/TelemetryJson.cpp` i obłożony testami, które sprawdzają
dosłowną treść JSON-a. Zmiana kształtu przesyłki psuje te testy; to celowe,
bo taka zmiana wymaga ruszenia także drugiej strony.

Wersja: 2 (2026-09-01). Zmiana wobec wersji 1: **przechył i prędkość są teraz
liczbami całkowitymi**, nie ułamkowymi — patrz sekcja 3.

---

## 1. Zasady

**Urządzenie jest offline'owe i nieufne wobec sieci.** Wysyła, gdy może, i
zakłada, że przesyłka mogła nie dojść albo że nie dotarło potwierdzenie.

**Numer przejazdu `seq`** rośnie monotonicznie przez całe życie urządzenia i
**nie jest zerowany przez reset wyników**. Para `(device_id, seq)` jest kluczem
unikalnym po stronie API. Powtórna wysyłka tego samego przejazdu nie może
tworzyć duplikatu — ma trafić w istniejący wpis.

**Kolejność wyników wynika z `seq`, nie z czasu.** Urządzenie nie ma zegara
czasu rzeczywistego — datę zna wyłącznie z modułu GPS (od 2026-09-03). Przejazd
odbyty bez zasięgu satelitów idzie z `recorded_at: null` i to jest stan
normalny, nie błąd. Data nigdy nie decyduje o kolejności.

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
      "lean_left_deg": 42,
      "lean_right_deg": 38,
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
| `rides[].recorded_at` | int lub null | unix timestamp UTC końca przejazdu z GPS; `null` gdy przejazd odbył się bez fixa |
| `rides[].duration_s` | int | czas trwania przejazdu w sekundach |
| `rides[].lean_left_deg` | **int** | maksymalny przechył w lewo, pełne stopnie |
| `rides[].lean_right_deg` | **int** | maksymalny przechył w prawo, pełne stopnie |
| `rides[].accel_g` | float, 2 miejsca | maksymalne przyspieszenie |
| `rides[].brake_g` | float, 2 miejsca | maksymalne hamowanie |
| `rides[].speed_kmh` | **int** lub null | prędkość maksymalna, pełne km/h; `null` gdy brak GPS |

**`speed_kmh` nigdy nie przyjdzie jako `0`.** Są tylko dwie możliwości:

- **`null`** — nie było czym zmierzyć (brak modułu GPS albo brak fixu).
  Na ekranie urządzenia widnieje wtedy `---` i strona powinna zrobić to samo.
- **liczba ≥ 1** — pomiar był. Odbiornik z ustaloną pozycją pokazuje na postoju
  szum rzędu 0,3–0,5 km/h; to jest pomiar, więc nie może wyglądać tak samo jak
  jego brak. Wartości poniżej 1 km/h podnosimy do `1`.

Dzięki temu kolumna prędkości ma jedno czytelne rozróżnienie: kreski znaczą
„nie wiemy", każda liczba znaczy „wiemy".

### Przesyłka dosłownie

Urządzenie składa JSON samo, bez biblioteki: **żadnych spacji, żadnych
znaków nowej linii, pola zawsze w tej samej kolejności**. Poniżej dokładnie to,
co pójdzie po kablu — ten sam ciąg znak w znak jest w
`test/test_telemetry/test_telemetry.cpp`:

```
{"device_id":"a1b2c3d4e5f6","fw":"1.0.0","calibrated":true,"rides":[{"seq":7,"recorded_at":null,"duration_s":1832,"lean_left_deg":42,"lean_right_deg":38,"accel_g":0.75,"brake_g":0.50,"speed_kmh":null}]}
```

Przechył i prędkość są **liczbami całkowitymi**. Przyspieszenie i hamowanie mają
zawsze **dwa miejsca po przecinku**, również gdy końcówka to zero: `0.50`, nie
`0.5`. Nie ma notacji wykładniczej ani `NaN`.

### Dlaczego przechył jest liczbą całkowitą

Bo taka wartość widnieje na ekranie urządzenia, a **jedna wielkość ma mieć jedną
liczbę**. Gdyby urządzenie wysyłało `25.1`, a strona zaokrąglała to sama,
w wąskim paśmie wyszłyby dwie różne odpowiedzi na to samo pytanie:

| Pomiar | Ekran urządzenia | Gdyby API dostawało `%.1f` | Panel po `round()` |
|---|---|---|---|
| 25,46° | 25 | `25.5` | **26** ✗ |

Zaokrąglenie działoby się dwa razy — drugi raz na wartości już przyciętej.
Teraz dzieje się raz, w jednym miejscu w firmware (`motion::roundHalfUp`),
wspólnym dla ekranu i dla przesyłki.

Druga racja jest pomiarowa: estymacja przechyłu z żyroskopu ma dokładność rzędu
**3–5 stopni**. Część dziesiętna sugerowałaby precyzję, której tam nie ma.

Siły zostają ułamkowe, bo akcelerometr mierzy je bezpośrednio — `0.97 g`
to prawdziwa wartość, a nie zaokrąglony `1 g`. Ekran pokazuje je tak samo.

**Zaokrąglenie: połówki w górę** (25,5 → 26). Nie bankierskie, jak w `printf`,
bo „24,5 daje 24, a 25,5 daje 26" jest nie do wytłumaczenia komuś, kto porównuje
dwie liczby na ekranie.

Pusta tablica `rides` jest poprawna i może się zdarzyć (urządzenie sprawdza
łączność). Odpowiedź na taką przesyłkę to bieżący `accepted_through` konta
dla tego urządzenia, albo `0`, gdy nic jeszcze nie przyszło.

### Odpowiedź

```json
{ "accepted_through": 7 }
```

Numer **ostatniego przejazdu przyjętego i zapisanego**. Urządzenie zapamiętuje
tę jedną liczbę jako znacznik „wysłane do numeru N" i przy następnej okazji
wysyła tylko to, co ma numer wyższy.

Konsekwencje dla implementacji:

- przejazdy przetwarzać **po kolei i w transakcji**; `accepted_through` ma być
  numerem ostatniego przejazdu zapisanego **bez przerwy w ciągu tej przesyłki** —
  jeśli przejazd nr 5 się nie zapisał, a 6 i 7 tak, w odpowiedzi ma być `4`,
- **dziura MIĘDZY `accepted_through` a pierwszym numerem w przesyłce nie jest
  błędem** — patrz niżej. Serwer ma taką przesyłkę przyjąć normalnie,
- pominięcie tego pola albo `null` znaczy dla urządzenia „nic nie przyjęto";
  wszystkie przejazdy zostają w kolejce do następnej próby,
- przejazd już znany (ten sam `device_id` i `seq`) jest **sukcesem**, nie
  konfliktem — ma podnieść `accepted_through`, a dane nadpisać przez upsert.

### Przejazdy, których urządzenie już nie ma

Historia w urządzeniu trzyma **dziesięć** przejazdów. Jedenasty wypycha
najstarszy — razem z jego danymi, których nikt już nie odtworzy. Jeśli w tym
czasie nie było łączności, te przejazdy **nigdy nie trafią na serwer**.

Wygląda to tak: serwer ma `accepted_through = 0`, a urządzenie przysyła
przejazdy 11–20. Dziura 1–10 jest trwała i żadna liczba ponowień jej nie
wypełni.

**Serwer musi taką przesyłkę przyjąć.** Traktowanie jej jak błędu zakleszcza
wysyłkę na zawsze: urządzenie wysyła to, co ma, serwer odrzuca, bo czeka na
przejazdy, które przepadły. Zasada jest prosta:

> Ciągłość obowiązuje **wewnątrz przesyłki**, nie między przesyłkami.
> Urządzenie gwarantuje, że wysyła najstarsze zaległe przejazdy w kolejności
> i bez przerw. Numer niższy od `accepted_through` to powtórka (upsert),
> luka powyżej to dane bezpowrotnie utracone.

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

## 6. Co zmienił moduł GPS (2026-09-03)

Kształt przesyłki się nie zmienił — **wypełniły się pola, które wcześniej były
`null`**: `recorded_at` i `speed_kmh`. Po stronie API nie ma nic do zrobienia,
o ile kolumny są nullable zgodnie z §5, ale panel powinien od teraz umieć
pokazać datę przejazdu i prędkość maksymalną.

Oba pola nadal **bywają puste i to jest poprawne**: przejazd w garażu
podziemnym nie ma ani daty, ani prędkości. Walidacja `present, nullable` musi
zostać taka, jaka jest.

Nadal do zrobienia po tej stronie: dane trasy (dystans, ślad — osobny endpoint,
patrz `docs/gpx-slad-trasy.md`).

---

## 7. Odbiór po stronie Laravela

### Tabela

```php
Schema::create('rides', function (Blueprint $table) {
    $table->id();
    $table->foreignId('user_id')->constrained();
    $table->string('device_id', 12)->index();
    $table->unsignedInteger('seq');
    $table->unsignedInteger('duration_s');
    $table->unsignedInteger('recorded_at')->nullable();  // brak RTC do czasu GPS
    $table->decimal('lean_left_deg', 4, 1);
    $table->decimal('lean_right_deg', 4, 1);
    $table->decimal('accel_g', 4, 2);
    $table->decimal('brake_g', 4, 2);
    $table->decimal('speed_kmh', 5, 1)->nullable();      // null != 0
    $table->string('fw', 16);
    $table->boolean('calibrated');
    $table->timestamps();
    $table->softDeletes();                               // kasowanie tylko miękkie

    $table->unique(['device_id', 'seq']);                // klucz idempotencji
});
```

Znacznik wysyłki trzyma urządzenie, nie serwer — ale serwer musi umieć odtworzyć
`accepted_through` z tabeli: `max(seq)` dla danego `device_id`.

### Walidacja

Kształt sprawdzać ostro, **zakresy wartości luźno**. Odrzucenie przesyłki kodem
422 nie kasuje jej z urządzenia — przejazd zostaje w kolejce i wróci przy każdej
kolejnej próbie, w kółko. Podejrzanie duży przechył lepiej zapisać i oznaczyć na
stronie niż odbić.

```php
public function rules(): array
{
    return [
        'device_id' => ['required', 'string', 'size:12', 'regex:/^[0-9a-f]{12}$/'],
        'fw'        => ['required', 'string', 'max:16'],
        'calibrated'=> ['required', 'boolean'],
        'rides'                     => ['present', 'array', 'max:10'],
        'rides.*.seq'               => ['required', 'integer', 'min:1'],
        'rides.*.recorded_at'       => ['present', 'nullable', 'integer'],
        'rides.*.duration_s'        => ['required', 'integer', 'min:0'],
        'rides.*.lean_left_deg'     => ['required', 'numeric'],
        'rides.*.lean_right_deg'    => ['required', 'numeric'],
        'rides.*.accel_g'           => ['required', 'numeric'],
        'rides.*.brake_g'           => ['required', 'numeric'],
        'rides.*.speed_kmh'         => ['present', 'nullable', 'numeric'],
    ];
}
```

`present` zamiast `required` przy polach dopuszczających `null` — urządzenie
zawsze te klucze wysyła, ale ich wartością bywa `null`.

### Kontroler

Sedno to policzenie `accepted_through` **jako ostatniego numeru bez przerwy
w ciągu**, a nie największego zapisanego:

```php
public function store(StoreRidesRequest $request)
{
    $data = $request->validated();
    $user = $request->user();

    $accepted = Ride::where('device_id', $data['device_id'])
        ->where('user_id', $user->id)
        ->max('seq') ?? 0;

    DB::transaction(function () use ($data, $user, &$accepted) {
        $previous = null;

        foreach ($data['rides'] as $ride) {         // już posortowane rosnąco
            // Ciągłość obowiązuje WEWNĄTRZ przesyłki. Dziura w środku znaczy,
            // że przesyłka jest uszkodzona — dalej nie idziemy.
            if ($previous !== null && $ride['seq'] !== $previous + 1) {
                break;
            }

            Ride::updateOrCreate(                   // powtórka to sukces, nie konflikt
                ['device_id' => $data['device_id'], 'seq' => $ride['seq']],
                [...$ride, 'user_id' => $user->id, 'fw' => $data['fw'],
                 'calibrated' => $data['calibrated']],
            );

            $accepted = max($accepted, $ride['seq']);
            $previous = $ride['seq'];
        }
    });

    return response()->json(['accepted_through' => $accepted]);
}
```

**Czego tu celowo NIE ma: sprawdzania, czy pierwszy numer w przesyłce jest
kolejnym po `accepted_through`.** Taki warunek wygląda rozsądnie, a zakleszcza
wysyłkę na zawsze, gdy urządzenie utraci najstarsze przejazdy przy przepełnieniu
historii — czeka wtedy na dane, których już nie ma. Potwierdzenie numeru
spoza bazy jest niemożliwe, bo `accepted_through` liczymy z faktycznie
zapisanych wierszy, nie z tego, co przyszło w żądaniu.

### Odpowiedź

Ciało odpowiedzi urządzenie czyta prostym wyszukaniem `accepted_through` —
nie ma na pokładzie parsera JSON. Wolno dokładać do odpowiedzi inne pola,
ale **nazwa `accepted_through` nie może wystąpić wcześniej w innym znaczeniu**
(np. w treści komunikatu błędu).

---

## 8. Sprawdzenie bez urządzenia

Przesyłka jest zwykłym POST-em, więc endpoint da się przetestować od razu:

```bash
curl -i https://motusy.top/api/v1/rides \
  -H 'Authorization: Bearer <token konta>' \
  -H 'Content-Type: application/json' \
  -H 'User-Agent: MotusyMotoBox/1.0.0' \
  -d '{"device_id":"a1b2c3d4e5f6","fw":"1.0.0","calibrated":true,"rides":[{"seq":1,"recorded_at":null,"duration_s":1832,"lean_left_deg":42.0,"lean_right_deg":38.0,"accel_g":0.75,"brake_g":0.50,"speed_kmh":null}]}'
```

Warto sprawdzić cztery rzeczy:

1. ten sam `curl` puszczony dwa razy daje `{"accepted_through":1}` i **jeden**
   wiersz w bazie,
2. przesyłka z `"rides":[]` odpowiada bieżącym numerem, nie błędem,
3. przesyłka zaczynająca się od `seq` wyższego niż stan bazy nie podnosi
   `accepted_through`,
4. zły token daje 401 — urządzenie po tym kodzie przestaje próbować, więc
   pomyłka w konfiguracji jest widoczna od razu, a nie po dobie.
