# Jak urządzenie ma wysyłać dane

Opis **działającego serwera** motusy.top, pisany dla strony firmware'u. Kontrakty
[`api-telemetria.md`](api-telemetria.md) i [`api-slad-trasy.md`](api-slad-trasy.md)
mówią, co wolno wysłać; ten dokument mówi, **co serwer naprawdę robi z tym,
co przyjdzie** — łącznie z rozstrzygnięciami, których w kontraktach nie było.

Stan na 4 września 2026, wszystko obłożone testami.

---

## 1. Skrót

| | Wynik przejazdu | Ślad trasy |
|---|---|---|
| Adres | `POST /api/v1/rides` | `POST /api/v1/devices/{device_id}/rides/{seq}/track` |
| Ciało | JSON, do 10 przejazdów naraz | plik `MMBT1` z flasha, bajt w bajt, do 1 MB |
| `Content-Type` | `application/json` | `text/plain; charset=us-ascii` |
| Odpowiedź | `{"accepted_through": N}` | `{"stored": true}` |
| Co kasuje z pudełka | przejazdy do numeru `N` włącznie | ten jeden ślad |

Do tego `GET /api/v1/ping` — 200 znaczy „token dobry", 401 „token zły".

**Kolejność obu wysyłek jest dowolna.** Ślad może pojechać przed wynikiem
przejazdu i zostanie przyjęty; serwer podepnie go do przejazdu, gdy ten dojdzie.
Brak śladu nigdy nie wpływa na `accepted_through`.

---

## 2. Nagłówki i token

```
Authorization: Bearer XFRS-34ST-YTS8
User-Agent: MotusyMotoBox/1.0.0
```

Token jest **jeden na konto**, nie na urządzenie. Przy sprawdzaniu serwer
normalizuje go: wielkość liter nie ma znaczenia, myślniki i spacje są pomijane,
więc `xfrs34styts8` przejdzie tak samo jak `XFRS-34ST-YTS8`. Alfabet nie zawiera
`0`, `O`, `1`, `I` ani `L` — jeśli w przepisanym tokenie taki znak się pojawi,
to jest to pomyłka przy przepisywaniu i serwer odpowie **401**.

Wszystko idzie po **HTTPS**. `User-Agent` nie jest sprawdzany, ale ułatwia
czytanie logów przy zgłoszeniach.

---

## 3. Ogranicznik — dotyczy obu endpointów

**60 żądań na minutę z jednego adresu IP**, liczone **przed** sprawdzeniem
tokena, więc nieudane próby też się liczą.

Przekroczenie daje **429**. To jest awaria przejściowa: ponawiać z rosnącym
opóźnieniem, nie kasować plików. Dla pudełka wysyłającego po zakończonej jeździe
limit jest nieosiągalny — chyba że firmware wpadnie w pętlę ponowień bez
odczekania.

Uwaga na wspólne IP: kilka pudełek za jednym NAT-em dzieli ten sam budżet.

---

## 4. Wynik przejazdu — `POST /api/v1/rides`

### Co serwer sprawdza

Zasada jest taka: **kształt ostro, zakresy luźno**. Odrzucenie przesyłki kodem
422 nie kasuje jej z urządzenia, więc podejrzanie duży przechył lepiej zapisać
niż odbić.

| Pole | Wymóg |
|---|---|
| `device_id` | 12 znaków `[0-9a-f]`; wielkie litery są sprowadzane do małych |
| `fw` | tekst, do 16 znaków |
| `calibrated` | wartość logiczna |
| `rides` | tablica, **najwyżej 10** pozycji; pusta jest poprawna |
| `rides[].seq` | całkowite, od 1 do 4 294 967 295 |
| `rides[].duration_s` | całkowite, nieujemne |
| `rides[].recorded_at` | klucz **musi być**, wartość może być `null` |
| `rides[].speed_kmh` | klucz **musi być**, wartość może być `null` |
| `rides[].lean_left_deg`, `lean_right_deg` | liczba, −999,9…999,9 |
| `rides[].accel_g`, `brake_g` | liczba, −99,99…99,99 |

Granice liczbowe wynikają z **pojemności kolumn**, nie z fizyki. Pola, których
nie ma w tabeli, są po cichu pomijane — dołożenie nowego do JSON-a niczego nie
zepsuje, ale też nic nie zapisze, dopóki serwer o nim nie wie.

`recorded_at` i `speed_kmh` mają **przychodzić zawsze**, choćby jako `null`.
Pominięcie klucza to 422, wysłanie `null` jest poprawne i znaczy „bez GPS-a".

### Co znaczy odpowiedź

```json
{ "accepted_through": 51 }
```

Wszystko do numeru 51 włącznie jest u nas na stałe — pudełko może to skasować.

Punktem wyjścia jest **najwyższy numer, jaki serwer ma dla tego urządzenia**,
a nie zero. Dlatego:

- **Pusta tablica `rides` to pytanie „na czym stoimy?"** — odpowiedź poda
  najwyższy zapisany numer, bez zapisywania czegokolwiek. To jest tańsze niż
  wysyłanie zaległości w ciemno.
- **Serwer nie wymaga ciągu od jedynki.** Licznik `seq` rośnie przez całe życie
  pudełka i nie jest zerowany; numery niższe od zapisanego maksimum są uznane
  za załatwione.
- **Dziura wewnątrz jednej przesyłki zatrzymuje przetwarzanie.** Wysłanie
  `[50, 51, 53]` da `accepted_through: 51`, a przejazd 53 **nie zostanie
  zapisany** — trzeba go przysłać ponownie. Potwierdzenie numeru, którego serwer
  nie zapisał, skasowałoby przejazd z pudełka bezpowrotnie.
- **Kolejność w tablicy nie ma znaczenia** — serwer sortuje po `seq` sam.

### Rzeczy, które zaskakują

**Nieznane pudełko dopisuje się samo** przy pierwszej udanej wysyłce, do konta
właściciela tokena. Nie ma osobnego parowania.

**Przejazd skasowany w panelu nie wraca**, ale liczy się jako przyjęty
i podnosi `accepted_through`. Dosyłanie go w kółko nic nie da.

**Cudze pudełko dostaje 403.** Jeśli `device_id` jest już przypisany do innego
konta, wysyłka nie przejdzie nigdy — to nie jest awaria do ponawiania, tylko
sygnał, że w konfiguracji jest zły token albo zły identyfikator.

---

## 5. Ślad trasy — `POST /api/v1/devices/{device_id}/rides/{seq}/track`

### Adres

```
POST /api/v1/devices/70041ddc6bc8/rides/51/track
```

**`device_id` w adresie musi być małymi literami.** Trasa dopuszcza wyłącznie
`[0-9a-f]{12}`, więc `70041DDC6BC8` da **404**, a nie 422 — i żadnej wskazówki,
co poszło źle. To jedyne miejsce, gdzie serwer nie wybacza wielkich liter.

`seq` musi być z zakresu 1…4 294 967 295. Poza nim: 422.

### Ciało

Plik z flasha bez przepakowywania:

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

Co serwer odrzuci kodem **422** (czyli: plik jest trwale zepsuty, ponowienie
nic nie da):

| Sytuacja | Uwaga |
|---|---|
| pierwsza linia inna niż `MMBT1` | nieznana wersja formatu |
| linia nagłówka bez `=` | |
| brak któregoś z `dev`, `fw`, `eps`, `t0`, `p0` | |
| `p0` inne niż dwie liczby całkowite | kolejność to **`lon,lat`** |
| `t0` nieliczbowe albo ujemne | `0` znaczy „czas nieznany" i jest poprawne |
| `eps` nieliczbowe, ujemne albo powyżej 65 535 | |
| linia punktu inna niż cztery liczby po przecinku | |
| `dt` ujemne | czas nie ma prawa cofać się |
| przechył powyżej 32 767 co do modułu | |
| współrzędna poza globem (±90°, ±180°) | zwykle znaczy zamienione `lon` z `lat` |
| `dev` w nagłówku inny niż w adresie | porównanie pomija wielkość liter |
| puste ciało żądania | |

Poza tym serwer jest tolerancyjny: końcówki linii `\r\n` przechodzą, nieznane
klucze w nagłówku są pomijane, dwa znaczniki `-` pod rząd nie tworzą pustego
segmentu, a przechył spoza zakresu ±60° jest zapisywany bez marudzenia.

### `t0=0`, czyli czas nieznany

Zero w `t0` unieważnia czas **całego śladu**, nie tylko punktu startowego.
Serwer nie sumuje wtedy `dt` do dat — inaczej drugi punkt wypadłby
1 stycznia 1970. W eksporcie GPX takie punkty nie mają znacznika `<time>`.

Praktycznie: jeśli pudełko złapie czas w połowie jazdy, lepiej domknąć plik
i zacząć nowy z prawdziwym `t0`, niż wysłać cały ślad z `t0=0`.

### Rozmiar

Do **1 MB**. Powyżej: **413**, czyli plik do skasowania. Realny sześciogodzinny
przejazd przy ε=8 m to około 50 kB, więc zapas jest ponad dwudziestokrotny —
przekroczenie oznacza raczej błąd zapisu niż długą jazdę.

### `Content-Type`

Serwer wymaga typu zaczynającego się od `text/` **albo braku nagłówka**.
Cokolwiek innego (`application/json`, `application/octet-stream`) daje **415**.
Brak nagłówka jest przepuszczany celowo, żeby przyszła wersja firmware'u nie
zakleszczyła się na tym polu.

### Idempotencja i kolejność

Ślad wisi na `(device_id, seq)`. Powtórna wysyłka **nadpisuje** poprzedni wpis
i zwraca 200 — powtórka jest sukcesem, nie konfliktem.

Ślad przyjęty przed wynikiem przejazdu czeka z pustym powiązaniem i podpina się
sam, gdy wynik dojdzie. Nie trzeba go wtedy wysyłać drugi raz.

**Ślad przejazdu skasowanego w panelu dostaje 200, ale nie jest zapisywany.**
Z punktu widzenia pudełka to sukces: plik ma zniknąć i nie wracać.

---

## 6. Kody odpowiedzi i co z nimi zrobić

| Kod | Kiedy | Reakcja urządzenia |
|---|---|---|
| **200** | zapisano, także powtórka | skasować to, co potwierdzone |
| **401** | token nieprawidłowy | **przestać próbować**, pokazać błąd do czasu zmiany konfiguracji |
| **403** | `device_id` należy do innego konta | jak wyżej — ponowienie nigdy nie pomoże |
| **413** | ślad powyżej 1 MB | skasować plik |
| **415** | `Content-Type` nietekstowy przy śladzie | traktować jak awarię, ponowić |
| **422** | plik albo JSON trwale niepoprawny | skasować plik / porzucić przesyłkę |
| **429** | przekroczony ogranicznik | ponowić z rosnącym opóźnieniem |
| **5xx**, brak odpowiedzi | awaria serwera, bazy albo dysku | ponowić z rosnącym opóźnieniem |

Dwa błędy kosztują najwięcej i oba są symetryczne:

- **422 na awarii przejściowej** kasuje dane bezpowrotnie;
- **5xx na trwale zepsutym pliku** każe budzić radio w kółko, aż do
  rozładowania baterii.

Serwer jest napisany tak, żeby nigdy nie oddać 500 za treść żądania — wszystkie
wartości spoza zakresu kolumn są łapane wcześniej i zwracane jako 422. Jeśli
mimo to przyjdzie 500, to jest to prawdziwa awaria po naszej stronie i ponawianie
ma sens.

---

## 7. Zalecany przebieg wysyłki

1. Po złapaniu WiFi: `GET /api/v1/ping`. 401 → zatrzymać się i pokazać błąd.
2. `POST /api/v1/rides` z zaległościami, **po dziesięć, od najstarszej**.
3. Skasować przejazdy do `accepted_through` włącznie.
4. Dla każdego skasowanego przejazdu, jeśli ma ślad — `POST .../track`.
   Nie trzeba czekać z tym na potwierdzenie wyniku; kolejność jest dowolna.
5. Powtarzać od punktu 2, dopóki `accepted_through` rośnie.

Punkt 4 na końcu, a nie na początku, ma jeden powód: przy słabym łączu lepiej
najpierw dowieźć wyniki wszystkich jazd niż jeden kompletny ślad.

---

## 8. Sprawdzenie curl-em

```bash
TOKEN=twoj-token-konta
BASE=https://motusy.top/api/v1

# Token
curl -s -o /dev/null -w '%{http_code}\n' "$BASE/ping" -H "Authorization: Bearer $TOKEN"

# Na czym stoimy — pusta tablica, nic nie zapisuje
curl -s "$BASE/rides" -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"device_id":"70041ddc6bc8","fw":"1.0.0","calibrated":true,"rides":[]}'

# Ślad
printf 'MMBT1\ndev=70041ddc6bc8\nfw=1.0.0\neps=8\nt0=1757001234\np0=1957648,5133528\n\n113,-182,5,12\n26,-3,1,-31\n-\n340,84,900,0\n' \
  > /tmp/slad.txt

curl -s -o /dev/null -w '%{http_code}\n' -X POST "$BASE/devices/70041ddc6bc8/rides/51/track" \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: text/plain; charset=us-ascii' \
  --data-binary @/tmp/slad.txt
```

Ślad z przykładu odtwarza się na cztery punkty w dwóch segmentach, w okolicach
Piotrkowa. **Jeśli na mapie wychodzi Afryka, `lon` zamieniło się z `lat`.**

---

## 9. Lista kontrolna dla firmware'u

- [ ] `device_id` w **adresie śladu** małymi literami — inaczej 404
- [ ] `recorded_at` i `speed_kmh` wysyłane zawsze, choćby jako `null`
- [ ] najwyżej 10 przejazdów w jednej przesyłce, od najstarszego
- [ ] kasowane wyłącznie to, co objęte przez `accepted_through`
- [ ] dziura w środku przesyłki → przejazdy powyżej niej wysłać jeszcze raz
- [ ] 401 i 403 zatrzymują wysyłkę; 429 i 5xx ją opóźniają; 413 i 422 kasują plik
- [ ] ślad wysyłany bez czekania na potwierdzenie wyniku
- [ ] `t0=0` tylko wtedy, gdy czasu nie zna **cały** ślad
- [ ] `Content-Type: text/plain` przy śladzie, `application/json` przy wynikach
- [ ] ponowienia z rosnącym opóźnieniem, żeby nie wejść w limit 60/min
