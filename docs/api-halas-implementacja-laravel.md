# Hałas w API — co dochodzi i co z tym zrobić po stronie Laravela

Dokument dla **strony serwera**. Mówi dokładnie, co urządzenie zaczęło wysyłać
od 2026-09-06, jak to przyjąć i jakich dwóch pomyłek nie popełnić przy
wyświetlaniu.

Kontrakt całości: [api-telemetria.md](api-telemetria.md).
Skąd się te liczby biorą: [pomiar-halasu.md](pomiar-halasu.md).

---

## 1. Nic nie znika, dochodzi pięć pól

Zmiana jest **wyłącznie addytywna**. Żadne istniejące pole nie zmienia nazwy,
typu ani znaczenia, więc:

- stare firmware nadal działa z nowym serwerem (nie wyśle nowych pól),
- nowe firmware nadal działa ze starym serwerem (pola zostaną zignorowane).

Adres, nagłówki, idempotencja po `(device_id, seq)`, `accepted_through` —
wszystko bez zmian.

### Było

```json
{
  "seq": 78,
  "recorded_at": 1757152800,
  "duration_s": 1832,
  "lean_left_deg": 42,
  "lean_right_deg": 38,
  "accel_g": 0.75,
  "brake_g": 0.50,
  "speed_kmh": 142
}
```

### Jest

```json
{
  "seq": 78,
  "recorded_at": 1757152800,
  "duration_s": 1832,
  "lean_left_deg": 42,
  "lean_right_deg": 38,
  "accel_g": 0.75,
  "brake_g": 0.50,
  "speed_kmh": 142,
  "max_noise_db": 108.4,
  "noise_at_speed_kmh": 62,
  "noise_clipped": 0,
  "noise_dropped": 0,
  "noise_cal": 1
}
```

---

## 2. Co znaczy każde pole

| pole | typ | znaczenie |
|---|---|---|
| `max_noise_db` | float, 1 miejsce, **nullable** | najgłośniejszy fragment przejazdu w dB(A) |
| `noise_at_speed_kmh` | int, **nullable** | prędkość w chwili ustanowienia tego rekordu |
| `noise_clipped` | int ≥ 0 | ile próbek dobiło do pełnej skali przetwornika |
| `noise_dropped` | int ≥ 0 | ile próbek przepadło na przerwach w strumieniu I²S |
| `noise_cal` | int ≥ 0 | znacznik serii pomiarowej |

### `max_noise_db` to nie jest zwykłe maksimum

To **najwyższy poziom, który utrzymał się co najmniej 5 sekund** (percentyl 90 %
w ruchomym oknie). Pojedyncze uderzenie kamienia w owiewkę czy trzask kasku
o bak są odrzucane **jakościowo**, a nie tłumione.

Ma to konsekwencję dla panelu: **wartość jest z natury niższa niż „najgłośniejszy
moment"** i taka ma być. Gdyby ktoś kiedyś chciał ją „poprawić" na zwykłe
maksimum, kolumna straciłaby sens po tygodniu — rekord ustanowiłby pierwszy
lepszy stuk i nigdy nie zostałby pobity.

### Zakres wartości

Realnie **50–126 dB(A)**. Sufit skali leży na 126,4 — wartość równa sufitowi
przy niezerowym `noise_clipped` znaczy „co najmniej tyle", patrz §5.

---

## 3. Migracja

```php
Schema::table('rides', function (Blueprint $table) {
    // null != cisza — patrz §5. Ta kolumna MUSI być nullable.
    $table->decimal('max_noise_db', 5, 1)->nullable();
    $table->unsignedSmallInteger('noise_at_speed_kmh')->nullable();

    // Diagnostyka. Przychodzi zawsze, także gdy pomiaru nie było,
    // więc default 0 jest poprawny — inaczej niż wyżej.
    $table->unsignedInteger('noise_clipped')->default(0);
    $table->unsignedInteger('noise_dropped')->default(0);
    $table->unsignedTinyInteger('noise_cal')->default(0);
});
```

Istniejące wiersze dostaną `null` w dwóch pierwszych kolumnach i to jest
**poprawny stan**: te przejazdy odbyły się przed mikrofonem, więc hałasu nie
mają. Nie wypełniać ich zerami.

---

## 4. Walidacja

```php
'rides.*.max_noise_db'       => ['sometimes', 'nullable', 'numeric'],
'rides.*.noise_at_speed_kmh' => ['sometimes', 'nullable', 'integer', 'min:0'],
'rides.*.noise_clipped'      => ['sometimes', 'integer', 'min:0'],
'rides.*.noise_dropped'      => ['sometimes', 'integer', 'min:0'],
'rides.*.noise_cal'          => ['sometimes', 'integer', 'min:0'],
```

> ⚠️ **`sometimes`, nie `present`.** Urządzenie z wcześniejszym firmware tych
> pól nie wysyła. Gdyby ich brak odrzucał całą przesyłkę przez 422, takie
> urządzenie **zakleszczyłoby się w terenie**: ponawiałoby wysyłkę w kółko,
> dostawało 422 i nigdy nie oddało przejazdów. To ta sama klasa błędu, co
> sprawdzanie ciągłości `seq` między przesyłkami — opisane
> w [api-telemetria.md](api-telemetria.md).

W kontrolerze, przy zapisie:

```php
'max_noise_db'       => $ride['max_noise_db']       ?? null,
'noise_at_speed_kmh' => $ride['noise_at_speed_kmh'] ?? null,
'noise_clipped'      => $ride['noise_clipped']      ?? 0,
'noise_dropped'      => $ride['noise_dropped']      ?? 0,
'noise_cal'          => $ride['noise_cal']          ?? 0,
```

---

## 5. Dwie pomyłki, które zamieniają tę kolumnę w bzdurę

### 5.1. `null` to nie zero

**Cicha jazda i martwy mikrofon to dwie różne rzeczy.** Urządzenie nie pokazuje
tej wartości na ekranie, więc jeśli mikrofon padnie, użytkownik **nie ma jak się
o tym dowiedzieć** — dowie się tylko z panelu, i tylko jeśli panel nie udaje,
że wszystko gra.

```
max_noise_db = null   ->  "—"  albo "nie mierzono"
max_noise_db = 0      ->  nie wystąpi
```

Ta sama zasada, co przy `speed_kmh`, i z tego samego powodu. Nie zastępować
`null` zerem ani przy zapisie, ani przy renderowaniu, ani w agregatach
(`AVG` ma pomijać `null`, nie liczyć go jako 0).

### 5.2. Przejazdów o różnym `noise_cal` nie wolno porównywać

`noise_cal` to **znacznik serii pomiarowej**. Zmienia się, gdy zmieni się
wzmocnienie mikrofonu albo montaż urządzenia w motocyklu.

Liczby z różnych serii są w tych samych jednostkach, ale **na przesuniętej
skali** — porównanie ich daje wynik, który wygląda sensownie i jest fałszywy.

```sql
-- Rekord sezonu: TAK
SELECT MAX(max_noise_db) FROM rides
 WHERE device_id = ? AND noise_cal = 1;

-- Rekord sezonu: NIE (miesza dwie skale)
SELECT MAX(max_noise_db) FROM rides WHERE device_id = ?;
```

W panelu wystarczy, żeby zestawienia grupowały po `noise_cal` albo filtrowały
po bieżącej wartości. Gdy pojawi się druga seria, warto pokazać to wprost —
„pomiary sprzed rekalibracji" jako osobna sekcja.

---

## 6. Kiedy liczbie nie wolno wierzyć

Dwa liczniki jadą **zawsze**, także przy `max_noise_db = null`. To one
odróżniają cichą jazdę od awarii.

| warunek | co to znaczy | jak pokazać |
|---|---|---|
| `noise_clipped > 0` | przetwornik obcinał sygnał, wynik jest **zaniżony** | „≥ 108,4 dB", nigdy „108,4 dB" |
| `noise_dropped > 0` | część przejazdu przepadła, pomiar **niepełny** | znacznik ostrzegawczy przy wierszu |
| oba zerowe | pomiar czysty | bez adnotacji |

Przy pojedynczych zdarzeniach (`noise_clipped` rzędu kilkudziesięciu próbek na
godzinny przejazd) to kosmetyka. Przy tysiącach — sygnał, że trzeba zejść ze
wzmocnieniem w firmware.

---

## 7. Zapytanie, które warto mieć od pierwszego dnia

Urządzenie siedzi w zadupku motocykla i **z góry nie da się rozstrzygnąć, czy
mikrofon słyszy wydech, czy pęd powietrza**. Wiatr rośnie monotonicznie
z prędkością, więc gdyby zdominował pomiar, `max_noise_db` przestałaby być
osobnym parametrem i stała się **drugą kolumną prędkości maksymalnej**, tylko
wyrażoną w decybelach.

Rozstrzyga to zestawienie dwóch pól, które i tak już są w tym samym wierszu:

```sql
SELECT seq,
       speed_kmh,
       noise_at_speed_kmh,
       max_noise_db,
       speed_kmh - noise_at_speed_kmh AS zapas
  FROM rides
 WHERE device_id = ?
   AND max_noise_db IS NOT NULL
   AND noise_at_speed_kmh IS NOT NULL
 ORDER BY seq DESC
 LIMIT 30;
```

**Jak to czytać:**

- `zapas` **rozrzucony**, rekordy hałasu padają przy połowie prędkości
  maksymalnej → słychać wydech, metryka mierzy to, co miała mierzyć;
- `zapas` **bliski zeru w każdym przejeździe** → mierzymy wiatr, `max_noise_db`
  nie niesie informacji o motocyklu i trzeba wrócić do tematu montażu mikrofonu.

Bez tego zapytania odpowiedź na to pytanie nie przyjdzie nigdy — obie sytuacje
produkują identycznie wyglądającą kolumnę `max_noise_db`.

---

## 8. Czego ta liczba NIE potrafi

Warto mieć to zapisane, zanim ktoś zapyta.

**Nie nadaje się do porównań między motocyklami.** Urządzenie jest zasłonięte,
we wnęce, w różnych motocyklach inaczej. Stała kalibracyjna została wyznaczona
w zakresie 64–72 dB(A) przy niepewności stanowiska ±3 dB, a mierzone jest
90–110 dB.

**Nadaje się w pełni do porównań tego samego urządzenia ze sobą** — i po to
powstała. Błędy systematyczne (rezonans wnęki, zasłonięcie, stała kalibracyjna)
są identyczne w maju i we wrześniu, więc w porównaniu przejazdu z przejazdem
się skracają.

To nie jest ostrzeżenie o wadzie, tylko opis tego, co ta kolumna znaczy:
**„gdzie w tym sezonie było najgłośniej"**, a nie „ile decybeli ma ten motocykl".
