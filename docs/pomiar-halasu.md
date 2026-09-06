# Motusy Moto Box — pomiar hałasu

Dokument projektowy. Powstał 2026-09-06, **przed napisaniem kodu** — spisuje decyzje
podjęte w rozmowie, żeby nie trzeba było ich odtwarzać z pamięci.

Punktem wyjścia jest [noice.md](noice.md) — opis toru pomiarowego przeniesionego
z drugiego urządzenia (DBMeterV1 na Atom Echo S3R). Tamten dokument mówi, **jak
mierzyć dźwięk**. Ten mówi, **co z tego bierzemy i gdzie to siada w naszym kodzie**.

---

## 1. Cel — i dlaczego jest inny, niż wygląda

Urządzenie siedzi w zadupku motocykla: zasłonięte, we wnęce, blisko wydechu.
Pomiar bezwzględny w dB(A) jest w takich warunkach fikcją i **nie próbujemy go
udawać**.

Chcemy czegoś innego:

> `max_noise` ma być **powtarzalny względem samego siebie**. Jeździsz cały sezon
> i widzisz, który przejazd był najgłośniejszy — tak samo, jak widzisz, który był
> najszybszy.

To nie jest słabsza wersja pomiaru. To **inny kontrakt**, z innym rozkładem
wymagań:

| | pomiar bezwzględny | nasz pomiar względny |
|---|---|---|
| stała kalibracyjna `K` | krytyczna | prawie nieistotna |
| błędy systematyczne (rezonans wnęki, zasłonięcie) | katastrofa | **za darmo** — identyczne w maju i we wrześniu |
| **niezmienność wzmocnienia i montażu** | ważna | **święta** |
| ALC / automatyczna regulacja | psuje | **niszczy całkowicie** |

Stąd cała dyscyplina tego etapu skupia się nie na dokładności, tylko na tym,
żeby nic po cichu nie przesunęło skali między przejazdami.

---

## 2. Decyzje przyjęte

| | |
|---|---|
| Przeliczniki | **1:1 z [noice.md](noice.md)** — ważenie A (IEC 61672), Fast τ = 125 ms, uśrednianie na mocy, 48 kHz |
| Metryka | `L_sus(5 s)` — maksimum utrzymane ≥ 5 s, percentyl 90 % okna |
| Okno | **5 s, na sztywno** (decyzja użytkownika: „na motocyklu 5 s to nie jest długo") |
| Wzmocnienie cyfrowe | **w dół o 12 dB** — `kAdcScaleStep` 7 → 5, sufit ~133 dB(A) |
| PGA | **bez zmian, 6 dB** |
| Ekran | **nie ruszamy.** Zero nowych wierszy, zero nowych widoków |
| Droga wyniku | **wyłącznie API**, razem z przejazdem |
| `noise_at_speed` | **dokładamy** — patrz §6 |
| Wiatr | mikrofon schowany; wiatr słyszalny, ale cichszy od wydechu (założenie do zweryfikowania danymi) |

---

## 3. Metryka — dlaczego nie zwykłe maksimum

To jest sedno całej rzeczy, więc warto zapisać rozumowanie, a nie sam wynik.

**Zwykłe `LAFmax` ustanowiłoby rekord sezonu pierwszego dnia i nigdy nie zostałby
pobity.** Kamień w owiewkę, trzask kasku o bak, klaśnięcie przy zmianie biegu —
każde z nich bije wydech o kilkanaście decybeli i trwa 50 ms. Kolumna „najgłośniejszy
przejazd" pokazywałaby wtedy „przejazd, w trakcie którego coś stuknęło".

To dokładnie ta sama patologia, którą opisaliśmy przy bramce prędkości
([architektura §16.1](architektura-techniczna.md)): *rekord przypadkowy zawsze
wygrywa z prawdziwym, bo jest większy*.

**Uśrednianie po 5 s też nie działa.** Pik 120 dB trwający 100 ms na tle 50 dB
daje `LAeq,5s = 103 dB` — stracił 17 dB i nadal wygrywa z każdym realnym warkotem.
Energia nie znika od tego, że podzielisz ją przez dłuższy czas.

**Co działa: minimum ruchome (erozja).** Definicja:

> `L_sus(5 s)` to najwyższy poziom `L` taki, że istnieje 5-sekundowy odcinek,
> w którym poziom `LAF` był przez cały czas równy `L` lub wyższy.

Przesuwasz po przebiegu okno 5 s, w każdym położeniu bierzesz minimum, na koniec
maksimum z tych minimów. Pik **znika całkowicie**, a nie „zostaje osłabiony".
Warunek jest jakościowy: albo hałas trwał 5 s, albo nie.

Metryka jest przy tym **monotoniczna** — `L_sus(5 s) ≤ L_sus(1 s) ≤ LAFmax`
zawsze. To dobry test jednostkowy i dobry argument: nie da się zarzucić, że zawyża.

### 3.1. Tolerancja 90 % — poprawka, bez której metryka nie działa

Czyste minimum jest zbyt surowe z dwóch powodów: motocykl faluje na obrotach,
a **sam filtr Fast potrzebuje czasu na narastanie** — przy τ = 125 ms dojście
do 0,5 dB od wartości docelowej zajmuje ~0,28 s. Dźwięk trwający **dokładnie**
5,0 s dałby pełny poziom tylko przez ~4,7 s i czyste minimum by go odrzuciło.
Metryka „≥ 5 s" nie wykrywałaby zdarzenia trwającego 5 s.

Zamiast minimum bierzemy więc **niski percentyl okna**: „przez co najmniej 90 %
czasu", czyli tolerancja zapadu 0,5 s. To jest ta sama operacja co `LA90`, tylko
liczona w oknie 5 s zamiast w minucie — nie wprowadzamy nowego pojęcia.

> ⚠️ Test `dokladnie_piec_sekund` jest najważniejszy w całym zestawie. To on
> złapie regresję, gdyby ktoś kiedyś „uprościł" tolerancję z powrotem do czystego
> minimum.

### 3.2. Implementacja — histogram ruchomy

Percentyl w ruchomym oknie liczy się histogramem: dodaj nową próbkę, usuń
najstarszą. Koszt stały, bez alokacji, bez sortowania.

Przy oknie 5 s i kroku 10 ms → 500 próbek:

| | rozmiar |
|---|---|
| bufor cykliczny indeksów koszyków (`uint16` × 500) | 1 000 B |
| histogram (`uint16` × 300 koszyków) | 600 B |
| **razem** | **1,6 kB** |

Skan 300 koszyków co 10 ms to 30 tys. operacji na sekundę. Niezauważalne.

---

## 4. Gdzie ta wartość mieszka — i dlaczego to jest łatwe

**Decyzja „nie idzie na ekran" rozstrzyga za nas najtrudniejszą część integracji.**

`motion::RideValues` opisuje **pięć liczb pokazywanych na ekranie**, a jego układ
w NVS jest przypięty do `kSchemaVersion`. [Store.cpp:15](../src/hal/Store.cpp#L15)
mówi wprost, ile kosztuje podniesienie tej wersji: **kasuje użytkownikowi
kalibrację montażu**, a bez niej pomiary celowo nie są zbierane.

Ale problem jest w repo rozwiązany już dwa razy. Komentarz
w [RideHistory.h:28](../lib/motion/RideHistory.h#L28):

> *Czas trwania jedzie OBOK wyników, a nie w `RideValues`, bo `RideValues` opisuje
> pomiar pokazywany na ekranie i jego układ w pamięci nieulotnej jest przypięty do
> wersji schematu — dołożenie tam szóstego pola skasowałoby użytkownikowi kalibrację.*

Czas trwania i znacznik czasu leżą więc w osobnych tablicach równoległych
i osobnych kluczach NVS (`rdur`, `hdur`, `hts`). **Hałas jest dokładnie tym samym
rodzajem wartości**: serwer jej potrzebuje, ekran nie. Idzie tą samą drogą:

```
telemetry::RideRecord      obok durationS i recordedAt
motion::RideHistory        kolejne tablice równoległe
NVS                        nowe klucze (rnoi / hnoi + wersje dla noise_at_speed)
```

**Zero migracji. Zero zmian w `RideValues`, `MainScreen`, `Theme.h`.
`kSchemaVersion` nietknięta.**

Konsekwencja przyjęta świadomie: skoro to nie jest `RideValues`, hałas **nie ma
pozycji w MAX OGÓLNIE**. I dobrze — rekord sezonu powstaje po stronie serwera,
z historii przejazdów. Urządzenie ma uczciwie oddać jeden przejazd.

---

## 5. Brak ekranu ma cenę: pomiar nie ma jak się poskarżyć

Przy prędkości maksymalnej pomyłkę widać od razu — patrzysz na ekran i wiesz.
Hałas zobaczysz pierwszy raz **po jeździe, na serwerze**. A martwy mikrofon nie
daje błędu, tylko liczbę: podłogę szumu, wyglądającą jak cicha jazda. Zepsuty
kanał I²S, przesterowanie, zgubione bloki DMA — wszystkie te awarie produkują
liczby całkowicie wiarygodne z wyglądu.

Dlatego **rekord niesie własną diagnozę**, nie samą wartość:

| pole | typ | po co |
|---|---|---|
| `max_noise` | dB(A), 0,1 dB | wynik `L_sus(5 s)` |
| `noise_at_speed` | km/h | prędkość w chwili rekordu — patrz §6 |
| `noise_clipped` | licznik | próbki dobite do skrajnego koszyka. Niezerowy → wynik jest „≥ X", nigdy „X" |
| `noise_dropped` | licznik | zgubione bloki I²S (przepełnienie DMA) → pomiar niepełny |
| `noise_cal` | mała liczba | znacznik wzmocnienia i montażu — patrz §7 |

Do tego **diagnostyka na porcie USB** — nie nowy ekran, tylko rozszerzenie
`pumpSerial()` w `main.cpp`, które już dziś rozdziela komendy konfiguracji
i rejestratora. Komenda `HALAS` wypisuje poziom bieżący, poziom utrzymany,
liczniki i informację, który kanał I²S niesie sygnał.

**To nie jest wygoda — to jedyne okno na pomiar aż do etapu N4**, więc bez niej
nie da się zaliczyć N0.

---

## 6. `noise_at_speed` — test na wiatr za darmo

Wydech i wiatr zachowują się przeciwnie:

- **wydech** jest głośny, gdy otwierasz gaz — może to być 40 km/h na dwójce;
- **wiatr** rośnie monotonicznie z prędkością — najgłośniej zawsze przy prędkości
  maksymalnej przejazdu, bez wyjątku.

Gdyby wiatr zdominował pomiar, `max_noise` przestałby być drugim parametrem
i stałby się **drugą kolumną `max_speed`**, tylko wyrażoną w decybelach.

A `max_speed` **już jest w tym samym rekordzie**, więc test nic nie kosztuje:

> **Jeśli `noise_at_speed` prawie zawsze równa się `max_speed` — mierzymy powietrze.**
> Rozrzut po prędkościach = słychać wydech.

Implementacja: w chwili, gdy `SustainedLevel` podnosi maksimum, przepisujesz
bieżące km/h obok. Jeden `uint16`, zero obliczeń. Drobiazg: `L_sus` orzeka
o oknie 5-sekundowym, więc zapisana prędkość pochodzi z **końca** tego okna —
dla tego zastosowania wystarczy.

Bez tego pola założenie o wietrze zostałoby założeniem na zawsze: obie sytuacje
produkują identycznie wyglądające `max_noise`.

---

## 7. Powtarzalność — jedyna rzecz, która może to zabić

Trzy rzeczy muszą być zamrożone na cały sezon: **wzmocnienie (PGA + ADC_VOLUME),
montaż, matematyka toru**. Każda ich zmiana przesuwa całą skalę.

Zabezpieczenie to jedna liczba w rekordzie — **`noise_cal`**. Zmieniasz gain
w firmware albo przekładasz urządzenie → podnosisz ją o jeden. Serwer wie wtedy,
że rekordy sprzed i po są dwiema seriami, a nie jedną.

Bez tego pierwsza aktualizacja firmware ruszająca wzmocnienie unieważni sezon
**po cichu** — liczby dalej będą wyglądać dobrze.

> ⚠️ **ALC i automute wyłączone: `0x18` = `0x00`.** Automatyczna regulacja
> wzmocnienia cicho zmienia nachylenie charakterystyki i jest jedyną rzeczą
> zdolną zepsuć powtarzalność **bez śladu w żadnym z liczników**. To ta sama
> pułapka, która zniszczyła pomiary na Raspberry Pi.

---

## 8. Wzmocnienie i stała `K` — miejsce, w którym łatwo się pomylić o 24 dB

Schodzimy ze wzmocnieniem **cyfrowym** o 12 dB: `kAdcScaleStep` **7 → 5**.

**Po co.** Mikrofon siedzi we wnęce blisko wydechu. Przy pierwotnym `K = 121,6`
pełna skala leżałaby na 121,6 dB(A) — realnie osiągalne, a przesterowany pomiar
jest po prostu nieprawdziwy i zaniżony. Po zejściu sufit rośnie do **~133 dB(A)**.
Podłogi nie żałujemy: w motocyklu nic ciszej niż 60 dB nie wystąpi.

**Podłoga zostaje tam, gdzie była** — w dB(A), nie w dBFS. Wynika to z pomiaru
z 30.08 w tamtym projekcie: szum rośnie dokładnie 1:1 ze wzmocnieniem cyfrowym,
czyli powstaje na wejściu przetwornika, nie w kwantyzacji 16-bit. Zapas na górze
dostajemy niemal za darmo.

> ⚠️ To wynika z **modelu, nie z pomiaru w tym zakresie**. Weryfikujemy komendą
> diagnostyczną w etapie N2, zanim się na tym oprzemy.

**`K` idzie W GÓRĘ, nie w dół.** Skoro to samo źródło daje teraz o 12 dB niższy
`dBFS`, to żeby `dB(A) = dBFS + K` dawało tę samą wartość:

```
   K = 121,6 + 12 = 133,6 dB          ← nowy punkt startowy
```

Zgadza się z sufitem: przy `dBFS = 0` (pełna skala) wynik to dokładnie
133,6 dB(A).

Piszę to tak dosłownie, bo **odwrócenie znaku dałoby liczby zaniżone o 24 dB,
wyglądające przy tym całkowicie wiarygodnie**. [noice.md §8](noice.md) opisuje
dokładnie taki wypadek — zła wartość `K` w bazie przesunęłaby pomiary o 32 dB.

**PGA zostaje na 6 dB.** Wzmocnienie analogowe przed przetwornikiem jest jedynym,
które realnie poprawia stosunek sygnału do szumu, a przy 6 dB nasycenia nie ma.
Ścinamy wyłącznie cyfrowe.

---

## 9. Bramkowanie — ta sama reguła co wszędzie

Rekord zbieramy tylko wtedy, gdy `SpeedGate` jest otwarta — dokładnie w tym samym
warunku co przechył, przyspieszenie i czas trwania przejazdu
([architektura §16.5](architektura-techniczna.md)). `RideMetrics.h` nazywa to
JEDNĄ REGUŁĄ i nie ma powodu jej łamać:

- **kręcenie gazem na postoju nie ustanawia rekordu** — to nie jest przejazd;
- wybieg 2 s po hamowaniu działa tak samo;
- przejazd zaczyna się od przekroczenia 5 km/h, nie od włączenia stacyjki.

### 9.1. Okno na granicy bramki — problem, który nim nie jest

Filtr A i ważenie Fast pracują **bez przerwy**, gdy mikrofon jest włączony
(zerowanie ich między oknami zafałszowałoby narastanie). Okno `SustainedLevel`
też jest karmione ciągle. Uzbrajamy wyłącznie **zapamiętywanie maksimum**,
dopiero po otwarciu bramki.

Co z oknem, które w połowie leży przed otwarciem bramki, czyli zawiera ciszę
z postoju? Odpowiedź daje sama konstrukcja metryki: skoro `L_sus` bierze **niski
percentyl** okna, cisza może wynik tylko **obniżyć**, nigdy podnieść. Błąd idzie
w stronę bezpieczną — możemy przegapić hałas z pierwszych pięciu sekund ruszania,
ale nie możemy niczego zawyżyć.

**Nie trzeba żadnego kodu na ten przypadek.**

---

## 10. Sprzęt — trzy rzeczy do pilnowania

**Kodek jest współdzielony z syreną.** ES8311 obsługuje i mikrofon, i głośnik,
ale nie naraz. [main.cpp:1397](../src/main.cpp#L1397) już dziś robi
`Speaker.begin()/end()` wokół sygnalizacji, więc konwencja istnieje. Rozdział jest
naturalny i **te dwa stany się nie przecinają**:

```
   mikrofon   →  wyłącznie w stanie Riding
   syrena     →  wyłącznie po uzbrojeniu alarmu
```

I²S musi być bezwarunkowo zwolnione przed wejściem w deep sleep.

**Osobne zadanie FreeRTOS na drugim rdzeniu.** Nie z powodu obciążenia — trzy
biquady przy 48 kHz to kilka procent CPU na S3 z FPU. Powód jest inny: pętla
główna jest superloopem, a **przepełnienie bufora DMA I²S nie zgłasza się błędem,
tylko po cichu gubi próbki**. Zadanie audio karmi się samo i oddaje do pętli jedną
liczbę; licznik `noise_dropped` jest siatką bezpieczeństwa, gdyby i to nie
wystarczyło.

**M5Unified może nie oddać mikrofonu na tej płytce.** Wersja 0.2.21 nie musi mieć
skonfigurowanego wejścia ES8311 dla StickS3. Plan B jest w [noice.md §2](noice.md) —
własna sekwencja rejestrów, z pułapkami:

- `0x09` / `0x0A` = `0x0C` — format 16-bit I²S, ustawiony **PRZED** `0x00 = 0x80`.
  Bez tego kodek nadaje 24 bity w 16-bitowe szczeliny i na wyjściu jest cyfrowe zero.
- `0x18` = `0x00` — ALC i automute wyłączone (§7).
- `0x02` = `0x18` (mnożnik ×8) wymaga BCLK 1,536 MHz, czyli 16 bitów × 2 kanały
  przy 48 kHz. Dlatego czytamy stereo i bierzemy jeden kanał, mimo jednego mikrofonu.
- `0x17` (ADC_VOLUME) — **nie zostawiać `0xFF` z M5Unified.** To +32 dB pod dyktando
  mowy; dla pomiaru zabiera cały zapas przed przesterowaniem.

Piny I²S: `MCK = 18, BCK = 17, WS = 15, DIN = 16` — zgodnie
z [architekturą §1](architektura-techniczna.md) **i** noice.md, oba źródła się
zgadzają. Mimo to **weryfikujemy energią obu kanałów**, nie zaufaniem do
dokumentacji: dokumentacja M5Stack już raz wprowadziła nas w błąd nazewnictwem
z perspektywy kodeka. Jeśli oba kanały są „idealnie ciche" — czytamy pin głośnika.

### 10.1. Montaż

- **Guma, nie sztywno do ramy.** MEMS reaguje na drgania konstrukcji tak samo jak
  na dźwięk; sztywny montaż znaczy, że częściowo mierzymy ramę. Ten sam wymóg,
  który już mamy dla IMU ([architektura §8](architektura-techniczna.md)).
- **Rezonans wnęki nas nie boli** — to błąd systematyczny, identyczny przez cały
  sezon. W metryce względnej systematyka jest za darmo (§1).
- **Przełożenie urządzenia = nowa seria pomiarowa.** Podnieść `noise_cal` (§7).

---

## 11. Struktura kodu

```
lib/noise/              ★ czyste C++, testowalne na Macu (env native)
  AWeighting            filtr A, trzy biquady, przepisany 1:1 z DBMeterV1
  TimeWeighting         Fast τ = 125 ms, uśrednianie NA MOCY
  SustainedLevel        histogram ruchomy, okno 5 s, percentyl 90 %
  NoiseMeter            fasada: powyższe + licznik przesterowań + offset K

src/hal/MicSource       ES8311 + I²S, w stylu GpsSource: sam sprawdza,
                        który kanał niesie sygnał, ma isSilent()

src/config.h            kNoiseWindowSec = 5.0
                        kNoiseTolerancePercent = 90
                        kNoiseAdcScaleStep = 5          (było 7)
                        kNoiseCalibrationDb = 133.6f    (patrz §8 — W GÓRĘ!)
                        kNoiseCalibrationVersion = 1
```

`lib/noise/` nie zna `M5Unified`, więc kompiluje się w środowisku `native`
i wszystkie testy idą na Macu — ta sama zasada, co `lib/motion` i `lib/track`.

### 11.1. Testy (env `native`), do `test/test_noise/`

| Test | Wejście | Oczekiwanie |
|---|---|---|
| `pik_jest_odrzucany` | tło 50 dB + pik 120 dB przez 100 ms | `L_sus` ≈ **50 dB**, nie 103 |
| `warkot_jest_zachowany` | 6 s ciągłego tonu 85 dB | `L_sus` ≈ **85 dB** (±0,5) |
| `za_krotki_nie_liczy_sie` | 3 s tonu 85 dB na tle 50 dB | `L_sus` ≈ **50 dB** |
| `dokladnie_piec_sekund` | 5,0 s tonu 85 dB | `L_sus` ≈ **85 dB** — pilnuje tolerancji z §3.1 |
| `seria_pikow_to_nie_warkot` | 10 pików co 0,5 s | `L_sus` = poziom tła |
| `monotonicznosc` | dowolny sygnał | `L_sus(5s) ≤ L_sus(1s) ≤ LAFmax` |

Wartości kontrolne filtru A: **−19,1 dB @ 100 Hz**, −3,2 @ 500 Hz,
**0,0 @ 1 kHz**, +1,2 @ 2 kHz, −1,1 @ 8 kHz.

---

## 12. Kontrakt API

Do [api-telemetria.md](api-telemetria.md) i `TelemetryJson` dochodzą pola z §5.

Zasada z `speed_kmh` przenosi się bez zmian: **brak pomiaru to `null`, nie zero.**
Przejazd z niedziałającym mikrofonem albo sprzed tej wersji firmware nie może
wyglądać jak cicha jazda.

Po stronie serwera: kolumna na wartość, kolumny na liczniki diagnostyczne
i `noise_cal`. Nic w istniejącym protokole się nie zmienia, więc stare urządzenie
i nowy serwer współpracują dalej.

---

## 13. Plan wdrożenia

| # | Klocek | Co powstaje | Kryterium zaliczenia |
|---|---|---|---|
| **N0** | Mikrofon żyje | `src/hal/MicSource`, komenda `HALAS` na USB | liczba na porcie reaguje na kręcenie gazem |
| **N1** | Algorytm | `lib/noise/` + testy native | 6 testów z §11.1 zielonych **na Macu** |
| **N2** | Wzmocnienie i zapas | `kAdcScaleStep` 5, `K` = 133,6, liczniki | silnik na obrotach bez przesterowań; podłoga zgodna z modelem |
| **N3** | Wpięcie w przejazd | zadanie audio, bramka, `RideRecord` + `RideHistory` + klucze NVS | wartość przeżywa cykl zasilania, `kSchemaVersion` nietknięta |
| **N4** | Kontrakt i wysyłka | `TelemetryJson` + `api-telemetria.md` + kolumny w Laravelu | przejazd z hałasem widoczny w panelu |
| **N5** | Jazda kontrolna | — | liczniki zerowe, `noise_at_speed` rozrzucone względem `max_speed` |

Trzy uwagi do kolejności:

- **N0 i N1 są niezależne.** N1 robi się w całości przy biurku — można zacząć
  od niego, jeśli mikrofon miałby sprawiać kłopoty.
- **N4 ma zależność poza firmware** — kolumny po stronie serwera. Warto rozszerzyć
  `api-telemetria.md` wcześnie, żeby migracja mogła powstawać równolegle.
- **N5 to pierwszy moment, w którym cokolwiek widać.** Do N4 włącznie jedynym
  oknem na pomiar jest port USB.

---

## 14. Rzeczy, których świadomie tu nie ma

- **Rozpoznawania źródła dźwięku.** Miernik ma mierzyć hałas, a nie zgadywać jego
  pochodzenie. Cała warstwa `NoiseCharacter` z DBMeterV1 zostaje tam, gdzie jest.
- **Detektora wiatru w oprogramowaniu** ([noice.md §6.1](noice.md) proponuje próg
  na stosunku energii nisko/po ważeniu A). Odłożone: najpierw `noise_at_speed`
  powie, czy problem w ogóle istnieje. Próg bez danych byłby zgadywaniem.
- **Hałasu w śladzie trasy.** Kusi (kolor trasy według głośności), ale wymaga
  bumpu formatu `MMBT1` → `MMBT2` i zmian po stronie serwera. Kandydat na osobny
  etap, po N5.
- **Kalibracji bezwzględnej.** `K = 133,6` jest punktem startowym dla wartości,
  która i tak ma sens tylko względny. Gdyby kiedyś miała trafić do porównań
  między motocyklami — patrz procedura w `KALIBRACJA.md` tamtego projektu,
  z kalibracją **w docelowym montażu** (sam obrót przyrządu dawał 7 dB).

---

## Skąd wzięte

- Tor pomiarowy, przeliczniki, pułapki ES8311, budżet histogramu: [noice.md](noice.md)
- Miejsce w strukturach i pułapka `kSchemaVersion`:
  [RideHistory.h](../lib/motion/RideHistory.h), [Store.cpp](../src/hal/Store.cpp)
- Bramka prędkości i reguła „rekord przypadkowy wygrywa z prawdziwym":
  [architektura-techniczna.md §16](architektura-techniczna.md)
- Piny I²S i skan magistrali: [architektura-techniczna.md §1](architektura-techniczna.md)
