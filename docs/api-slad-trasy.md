# Motusy Moto Box — kontrakt API śladu trasy

Uzupełnienie [`api-telemetria.md`](api-telemetria.md). Ten dokument opisuje
**drugi, niezależny endpoint**: ślad przejazdu. Zasady z tamtego kontraktu
(token, `device_id`, HTTPS, brak kasowania) obowiązują tu bez zmian.

**Wersja formatu: 1** (`MMBT1`). Format zamrożony 2026-09-04 i pokryty testami
w `test/test_track_format/` — napisy w tym dokumencie są tymi samymi napisami,
które sprawdzają testy.

---

## 1. Zasady

**Ślad jest dodatkiem, nie zamiennikiem wyniku.** Wynik przejazdu idzie tak jak
dotąd, przez `POST /api/v1/rides`. Ślad to osobne żądanie, którego brak niczego
nie psuje. **Brak śladu nigdy nie blokuje `accepted_through`** — te dwie rzeczy
nie mają prawa się o siebie zahaczać.

**Ślad jest opcją, domyślnie wyłączoną.** Trasa to dane innej wagi niż kąt
przechyłu, więc wymaga świadomej zgody. Wyłączona opcja znaczy, że urządzenie
nic nie zapisuje — nie „zapisuje, ale nie wysyła".

**Ślady są prywatne.** Widoczne wyłącznie dla właściciela konta. Nie ma
publicznego widoku, nie ma udostępniania linkiem.

**Ślad jest kompletny.** Nie ucinamy początku ani końca. Standardowa rada „utnij
pierwsze 200 m, bo tam jest dom właściciela" rozwiązywałaby problem, którego tu
nie ma, a odbierała dane, po które użytkownik przyszedł.

**Ciało żądania to plik z flasha, bajt w bajt.** Urządzenie nie przepakowuje
niczego przy wysyłce: plik powstaje w trakcie jazdy w tym samym formacie, w
którym leci na serwer. Dlatego format jest tekstowy i liniowy, a nie JSON.

---

## 2. `POST /api/v1/devices/{device_id}/rides/{seq}/track`

```
POST /api/v1/devices/70041ddc6bc8/rides/51/track
Authorization: Bearer <token konta>
Content-Type: text/plain; charset=us-ascii
User-Agent: MotusyMotoBox/1.0.0
```

`device_id` i `seq` są **w adresie, nie w ciele**. Powód jest konkretny: numer
przejazdu nadaje się dopiero przy archiwizacji, czyli **po** zakończeniu jazdy,
a plik powstaje w jej trakcie. Gdyby `seq` musiał być w treści, urządzenie
musiałoby przepisywać plik po fakcie.

### Ciało żądania — dosłownie

```
MMBT1
dev=70041ddc6bc8
fw=1.0.0
eps=8
t0=1757001234
p0=1957648,5133528

113,-182,5,12
26,-3,1,-31
-
340,84,900,0
```

Pierwsza linia to magia z numerem wersji. Potem nagłówek `klucz=wartość`, jedna
para na linię. **Pusta linia kończy nagłówek.** Wszystko po niej to punkty,
jeden na linię.

| Klucz | Znaczenie |
|---|---|
| `dev` | `device_id`, 12 znaków hex — do sprawdzenia zgodności z adresem |
| `fw` | wersja firmware, która zapisała ślad |
| `eps` | szerokość korytarza w metrach użyta przy zapisie (patrz §4) |
| `t0` | unix timestamp **UTC** punktu startowego; `0` gdy czas nieznany |
| `p0` | punkt startowy: `lon,lat` w jednostkach 1e-5 stopnia |

### Linia punktu

```
dlon,dlat,dt,lean
```

| Pole | Typ | Znaczenie |
|---|---|---|
| `dlon` | int | przyrost długości geograficznej w jednostkach 1e-5 stopnia |
| `dlat` | int | przyrost szerokości geograficznej w jednostkach 1e-5 stopnia |
| `dt` | int ≥ 0 | sekundy od poprzedniego punktu |
| `lean` | int, −60…60 | **maksymalny** przechył na odcinku od poprzedniego punktu, `+` = w prawo |

**Kolejność to `lon,lat`, nie `lat,lon`.** Pomyłka w tym miejscu daje ślad
gdzieś w Somalii i nikt tego nie zauważy od razu.

**Wszystkie przyrosty liczą się względem poprzedniego punktu**, a pierwszy
względem `p0`/`t0`. Odtworzenie to zwykłe sumowanie. Wartości są całkowite, więc
sumowanie jest dokładne i **błąd się nie kumuluje** — po sześciu godzinach
ostatni punkt jest tak samo dobry jak pierwszy.

**`lean` to maksimum z odcinka, nie odczyt chwilowy.** Punkt zastępuje kawałek
trasy, więc niesie najmocniejszy przechył z tego kawałka. Dzięki temu mapa
pokolorowana kątem nie gubi najciekawszego miejsca. Przechył punktu startowego
nie występuje — przed nim nie ma odcinka.

### Linia przerwy

```
-
```

Linia zawierająca **wyłącznie myślnik** znaczy: *podnieś ołówek*. Punkt po niej
zaczyna nowy segment i **nie wolno rysować linii** od punktu poprzedniego do
niego. Tunel, wiadukt, garaż, dłuższy postój.

**Przyrosty płyną dalej przez przerwę.** Znacznik mówi tylko tyle, że między
tymi punktami nie było jazdy — nie zeruje sumowania. W GPX odpowiada temu nowy
`<trkseg>` w tym samym `<trk>`.

### Odpowiedź

```json
{ "stored": true }
```

Kod 200 znaczy „ślad zapisany" i urządzenie oznacza go jako dostarczony.
Cokolwiek innego oznacza „spróbuję jeszcze raz".

### Kody odpowiedzi

| Kod | Znaczenie | Reakcja urządzenia |
|---|---|---|
| 200 | zapisano (także powtórka) | oznacza ślad jako dostarczony, kasuje plik |
| 401 / 403 | token nieprawidłowy | **przestaje próbować**, tak samo jak przy wynikach |
| 413 | ślad za duży | **kasuje plik** — ponowienie nic nie da |
| 415 | zły `Content-Type` | traktuje jak błąd serwera |
| 422 | nieznana wersja formatu albo plik niepoprawny | **kasuje plik**, ponowienie nic nie da |
| 429, 5xx, brak odpowiedzi | awaria | ponawia z rosnącym opóźnieniem |

Rozróżnienie 413/422 od 5xx jest istotne: błąd trwały ma skasować plik, a nie
budzić radio co kilka minut aż do rozładowania baterii.

### Idempotencja

Ślad wisi na **`(device_id, seq)`** — tym samym kluczu co przejazd. Powtórna
wysyłka ma nadpisać istniejący wpis, nie utworzyć drugiego, i zwrócić 200.

**Ślad może dojść przed wynikiem przejazdu.** Wysyłki są niezależne, a kolejność
zależy od tego, kiedy urządzenie złapało sieć. Serwer ma przyjąć ślad dla `seq`,
którego jeszcze nie zna, i połączyć go z przejazdem, gdy ten dojdzie. Odsyłanie
404 zakleszczyłoby wysyłkę w tę i z powrotem.

---

## 3. Rozmiar

Przy korytarzu ε=8 m i module 1 Hz:

| Trasa | Punktów | Rozmiar |
|---|---|---|
| godzina miasta | ~600 | ~8 kB |
| 3 h trasy mieszanej | ~1 800 | ~25 kB |
| 6 h, najdłuższy realny przejazd | ~3 600 | ~50 kB |

Linia punktu to 10–16 bajtów. **Serwer powinien przyjmować do 1 MB** — zapas na
wypadek złych warunków (gęsta zabudowa mnoży punkty) i na przyszłe wersje
formatu. Powyżej: 413.

---

## 4. Po co `eps` w nagłówku

Szerokość korytarza opisuje **dokładność zapisanego śladu**: gwarancja brzmi
*ślad nigdzie nie odbiega od prawdziwej trasy więcej niż ε metrów*. Bez tej
liczby danych z różnych wersji firmware nie da się uczciwie porównać ani
policzyć dystansu z sensownym błędem.

Do liczenia dystansu: suma odcinków między punktami zaniża wynik na zakrętach
(cięciwa zamiast łuku), a przy ε=8 m błąd na typowej trasie jest rzędu 1%.
Dla licznika kilometrów to bez znaczenia; dla „długości przejazdu co do metra"
— nie jest to właściwe źródło.

---

## 5. Po stronie bazy

- tabela osobna od `rides`, np. `ride_tracks`,
- unikalny indeks na **`(device_id, seq)`**,
- powiązanie z `rides` **nullable** — ślad może dojść pierwszy,
- treść trzymana jako plik albo kolumna tekstowa; **nie parsować przy zapisie**,
  tylko przy renderowaniu (albo raz, do tabeli punktów, w kolejce),
- kasowanie wyłącznie jako soft-delete, tak samo jak przejazdy,
- ślad ma dziedziczyć prywatność konta — żadnego publicznego adresu do pliku.

---

## 6. Odbiór po stronie Laravela

### Trasa

```php
Route::post('/v1/devices/{deviceId}/rides/{seq}/track', [TrackController::class, 'store'])
    ->middleware('auth:sanctum')
    ->where(['deviceId' => '[0-9a-f]{12}', 'seq' => '[0-9]+']);
```

### Parser

Ciało jest tekstem, nie JSON-em — `$request->getContent()`, nie `$request->input()`.

```php
public function parse(string $body): array
{
    $lines = explode("\n", $body);

    if (($lines[0] ?? '') !== 'MMBT1') {
        abort(422, 'nieznana wersja formatu sladu');
    }

    $header = [];
    $i = 1;
    for (; $i < count($lines) && $lines[$i] !== ''; $i++) {
        [$key, $value] = explode('=', $lines[$i], 2);
        $header[$key] = $value;
    }
    $i++; // pusta linia konczaca naglowek

    [$lon, $lat] = array_map('intval', explode(',', $header['p0']));
    $time = (int) $header['t0'];

    $segments = [];
    $current  = [['lon' => $lon / 1e5, 'lat' => $lat / 1e5, 'at' => $time, 'lean' => null]];

    for (; $i < count($lines); $i++) {
        $row = $lines[$i];
        if ($row === '') {
            continue; // koncowa nowa linia
        }
        if ($row === '-') {
            // Podnies olowek: nowy segment, ale sumowanie plynie dalej.
            $segments[] = $current;
            $current = [];
            continue;
        }

        [$dlon, $dlat, $dt, $lean] = array_map('intval', explode(',', $row));
        $lon  += $dlon;
        $lat  += $dlat;
        $time += $dt;

        $current[] = [
            'lon'  => $lon / 1e5,
            'lat'  => $lat / 1e5,
            'at'   => $time,   // unix UTC; 0 gdy urzadzenie nie znalo czasu
            'lean' => $lean,   // + = w prawo
        ];
    }

    $segments[] = $current;

    return ['header' => $header, 'segments' => $segments];
}
```

Uwaga na dwa miejsca, w których łatwo się pomylić:

1. **`lon` jest pierwsze.** Większość bibliotek map przyjmuje `[lat, lng]` —
   przy `Leaflet` czy `Google Maps` trzeba je zamienić, i to jest dokładnie ten
   błąd, który daje ślad w Somalii.
2. **Sumowanie nie resetuje się na `-`.** Nowy segment zaczyna się od punktu
   policzonego z bieżącej sumy, a nie od `p0`.

### Eksport do GPX

Każdy segment to osobny `<trkseg>` w jednym `<trk>`. Punkt z `at = 0` (czas
nieznany) zapisywać bez `<time>`, a nie z epoką — data 1970 w pliku GPX myli
większość programów bardziej niż jej brak.

---

## 7. Sprawdzenie bez urządzenia

```bash
TOKEN=twoj-token-konta

printf 'MMBT1\ndev=70041ddc6bc8\nfw=1.0.0\neps=8\nt0=1757001234\np0=1957648,5133528\n\n113,-182,5,12\n26,-3,1,-31\n-\n340,84,900,0\n' \
  > /tmp/slad.txt

# Zapis sladu
curl -i -X POST https://motusy.top/api/v1/devices/70041ddc6bc8/rides/51/track \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: text/plain; charset=us-ascii" \
  --data-binary @/tmp/slad.txt

# Powtorka tego samego zadania: ma dac 200 i JEDEN wpis w bazie, nie dwa
curl -i -X POST https://motusy.top/api/v1/devices/70041ddc6bc8/rides/51/track \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: text/plain; charset=us-ascii" \
  --data-binary @/tmp/slad.txt

# Zly format: ma dac 422, nie 500
printf 'MMBT9\n\n' | curl -i -X POST https://motusy.top/api/v1/devices/70041ddc6bc8/rides/51/track \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: text/plain; charset=us-ascii" \
  --data-binary @-
```

Przykład wyżej odtwarza się na dwa segmenty: `[1957648,5133528] → [1957761,5133346]
→ [1957787,5133343]`, przerwa, `[1958127,5133427]`. W stopniach:
`19.57648, 51.33528` i tak dalej — to okolice Piotrkowa, więc jeśli wychodzi Ci
Afryka, zamieniłeś `lon` z `lat`.

---

## 8. Czego jeszcze nie ma

Te rzeczy są rozstrzygnięte po stronie firmware, ale **nie wpływają na kontrakt**
— zapisane tu, żeby nie wracały jako pytania:

- **Włączenie opcji w trakcie jazdy** zaczyna ślad od tej chwili; wyłączenie
  domyka plik i to, co zebrane, i tak zostanie wysłane.
- **Ślad żyje tak długo jak przejazd w historii urządzenia** (10 wpisów).
  Jedenasty przejazd wypycha najstarszy razem z jego śladem, także wtedy, gdy
  ślad nie zdążył pojechać na serwer. Ślad jest dodatkiem — nie warto dla niego
  komplikować cyklu życia historii.
- **Prędkość w punkcie** nie jest wysyłana. Format jest pozycyjny, więc piąte
  pole da się dołożyć bez przebudowy, gdyby kiedyś było potrzebne.
