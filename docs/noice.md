# Miernik motocyklowy — dB(A) z maksimum utrzymanym ≥ 5 s

Punkt wyjścia dla **drugiego urządzenia**: [M5StickS3](https://docs.m5stack.com/en/core/StickS3)
(ESP32‑S3‑PICO‑1, 8 MB Flash / 8 MB PSRAM) montowanego w motocyklu, z GPS,
zbierającego dane lokalnie i wysyłającego je po zakończeniu jazdy.

Dokument mówi **co przenieść z DBMeterV1 dosłownie**, **czego przenieść nie wolno**
i **jak policzyć nową metrykę** — hałas, który utrzymał się co najmniej 5 sekund.

---

## 1. Zasada nadrzędna: co jest przelicznikiem, a co pomiarem egzemplarza

To jest najważniejszy rozdział w tym dokumencie, bo w Twoim pytaniu było założenie,
które trzeba rozbroić: *„muszę założyć, że przeliczniki będą identyczne w obu urządzeniach"*.

Częściowo tak. Ale nie wszystko, co wygląda na przelicznik, nim jest.

```
   ┌─────────────────────────────────────────────────────────────────┐
   │  PRZENOSI SIĘ 1:1 — i MUSI, inaczej liczby są nieporównywalne  │
   ├─────────────────────────────────────────────────────────────────┤
   │  • filtr ważenia A (IEC 61672) — f₁…f₄, K normalizujące        │
   │  • ważenie czasowe Fast, τ = 125 ms, uśrednianie NA MOCY        │
   │  • umowa zera skali: 10·log10(średnia z x²), x ∈ ⟨−1, 1⟩        │
   │  • częstotliwość próbkowania 48 kHz                             │
   │  • histogram percentyli: −140…+10 dB, koszyk 0,5 dB             │
   │  • definicja LAeq, LAFmax, LA10/50/90                           │
   └─────────────────────────────────────────────────────────────────┘

   ┌─────────────────────────────────────────────────────────────────┐
   │  NIE PRZENOSI SIĘ — to własność egzemplarza i jego montażu      │
   ├─────────────────────────────────────────────────────────────────┤
   │  • stała kalibracyjna K = 121,6 dB                              │
   │  • ustawienie wzmocnienia (PGA / ADC_SCALE / ADC_VOLUME)         │
   │  • poziom szumu własnego (podłoga zakresu)                      │
   │  • piny I2S                                                     │
   └─────────────────────────────────────────────────────────────────┘
```

**Matematyka jest przelicznikiem. Kalibracja jest pomiarem.** Skopiowanie `K = 121,6`
do drugiego urządzenia to nie jest „zachowanie identycznych przeliczników" — to jest
założenie, że dwa różne kawałki plastiku z różnymi otworami akustycznymi mają tę samą
czułość. W [config.h](../include/config.h) jest zapisane, ile to kosztuje:

> Zmierzone tego samego dnia: obrócenie przyrządu względem źródła zmieniło odczyt
> o **7 dB**, a przełożenie ESP32 tak, by otwór mikrofonu nie patrzył w blat — o ok. **4 dB**.

Siedem decybeli z samego obrotu. Inna obudowa i montaż w motocyklu to co najmniej tyle.

**Wniosek roboczy:** `K = 121,6` jest **punktem startowym**, nie wartością docelową.
Spodziewaj się, że wyjdzie w okolicy 115–128 dB. Procedura wyznaczenia — § 8.

---

## 2. Sprzęt — czym StickS3 różni się od Atom Echo S3R

Dobra wiadomość, i to lepsza, niż się spodziewałem: **to jest ten sam tor analogowy.**

| | Atom Echo S3R (DBMeterV1) | M5StickS3 (motocykl) |
|---|---|---|
| Procesor | ESP32‑S3‑PICO‑1 | ESP32‑S3‑PICO‑1 ✅ ten sam |
| Kodek | **ES8311** @ I2C 0x18 | **ES8311** @ I2C 0x18 ✅ ten sam |
| Mikrofon | MEMS, SNR 65 dB | MEMS, SNR 65 dB ✅ ta sama klasa |
| Wzmacniacz | — | AW8737 (nieużywany) |
| I2C | — | SDA **G47**, SCL **G48** |
| I2S MCLK | G11 | **G18** |
| I2S BCLK | G17 | **G17** |
| I2S WS/LRCK | G3 | **G15** |
| I2S DIN (mikrofon → ESP32) | G4 | **G16** |
| Ekran | brak | LCD 1,14" ST7789P3 |
| IMU | — | **BMI270** @ 0x68 |
| Grove | tak | G9 / G10 |
| Bateria | brak | 250 mAh |

To znaczy, że **cała sekwencja rejestrów ES8311 z [AudioInput.cpp](../src/AudioInput.cpp)
przenosi się bez zmiany** — łącznie z pułapkami, które kosztowały nas najwięcej czasu:

- `0x09` / `0x0A` = `0x0C` — **format 16‑bit I2S, ustawiony PRZED `0x00 = 0x80`.**
  Bez tego kodek nadaje 24 bity w 16‑bitowe szczeliny i na wyjściu jest cyfrowe zero.
- `0x18` = `0x00` — **ALC i automute wyłączone.** To jest pułapka, która zniszczyła
  pomiary na Raspberry Pi: kompresor cicho zmienia nachylenie charakterystyki.
- `0x02` = `0x18` (mnożnik ×8) wymaga BCLK = 1,536 MHz, czyli **16 bitów × 2 kanały**
  przy 48 kHz. Dlatego czytamy stereo i bierzemy jeden kanał, mimo że mikrofon jest jeden.
- `0x17` (ADC_VOLUME) — **nie zostawiaj `0xFF` z M5Unified.** To +32 dB pod dyktando
  mowy; dla pomiaru zabiera cały zapas przed przesterowaniem.

Zmienia się **tylko mapa pinów**. Sensownie jest wyciągnąć ją z `AudioInput.h` do
struktury przekazywanej do `begin()`, żeby jedno źródło kodu obsłużyło obie płytki.

> ⚠️ **Nie ufaj nazwom pinów z karty katalogowej.** W Atom Echo S3R dokumentacja
> podaje „DOUT (Mic) = G48" z perspektywy **kodeka**, a z perspektywy ESP32 dane
> mikrofonu wchodzą na G4. Dla StickS3 tabela wyżej podaje **DIN = G16 z perspektywy
> ESP32** — ale zweryfikuj to pierwszym uruchomieniem: `begin()` już teraz mierzy
> energię obu kanałów i mówi, który niesie sygnał. Jeśli oba są „idealnie ciche",
> czytasz pin głośnika.

### GPS

StickS3 nie ma GPS na pokładzie — wchodzi przez **Grove (G9/G10) jako UART NMEA**
albo przez Hat2‑Bus. Do naszych celów potrzebne są z niego tylko cztery pola:
`lat`, `lon`, `speed`, `fix/sats`. Traktuj GPS jako **kanał całkowicie osobny od
pomiaru** — dokładnie tak, jak ENV III w DBMeterV1: brak fixa nie może wstrzymywać
ani zniekształcać pomiaru hałasu, ma tylko oznaczyć rekord jako pozbawiony pozycji.

---

## 3. Tor pomiarowy — identyczny, plus jeden nowy stopień

```
    próbki int16 z ES8311  (48 kHz)
          │
          ├──► wykrycie przesterowania      ⚠️ miernik, który cicho obcina, KŁAMIE
          ├──► normalizacja do ±1.0         x = s / 32768
          ├──► AWeighting                   filtr A, IEC 61672 (bez zmian)
          ├──► TimeWeighting Fast           τ = 125 ms, na mocy (bez zmian)
          │
          ├──► StatsWindow                  LAeq, LAFmax, LA10/50/90 (bez zmian)
          └──► SustainedLevel  ◄── NOWE     maksimum utrzymane ≥ 5 s
```

Nowy stopień **nie modyfikuje niczego powyżej**. Podpina się do tego samego strumienia
odczytów `LAF`, który już dziś co 10 ms trafia do histogramu percentyli
([LevelMeter.cpp:87‑90](../lib/meter/LevelMeter.cpp#L87-L90)). To ważne: nowa metryka
jest **dodatkiem**, a nie zamianą. Jeśli kiedyś okaże się źle nastrojona, LAeq i LAFmax
pozostają nietknięte i porównywalne z pierwszym urządzeniem.

---

## 4. Przeliczniki — konkretne liczby do przepisania

### 4.1 Ważenie A (IEC 61672‑1)

```
                 K · s⁴
   H(s) = ─────────────────────────────
          (s+ω₁)² (s+ω₂) (s+ω₃) (s+ω₄)²

   f₁ = 20.598997 Hz      f₃ = 737.86223 Hz
   f₂ = 107.65265 Hz      f₄ = 12194.217 Hz
   K  = ω₄² · 10^(1.9997/20)        ← normalizacja do 0 dB przy 1 kHz
```

Rozbite na trzy biquady i przeniesione transformacją biliniową — kod w
[AWeighting.cpp](../lib/meter/AWeighting.cpp), **przepisz bez zmiany ani jednej cyfry.**

Wartości kontrolne do testu: **−19,1 dB @ 100 Hz**, −3,2 @ 500 Hz, **0,0 @ 1 kHz**,
+1,2 @ 2 kHz, −1,1 @ 8 kHz.

> **Dlaczego 48 kHz, a nie 16.** Transformacja biliniowa deformuje charakterystykę
> tym mocniej, im bliżej Nyquista. Błąd przy 8 kHz (górna granica pasma wymaganego
> przez normę): **0,54 dB @ 48 kHz**, ale **1,47 dB @ 32 kHz**. Motocykl ma sporo
> energii wysoko (ssanie, łańcuch, wydech), więc to nie jest akademickie.

### 4.2 Ważenie czasowe

```
   msq[n] = msq[n−1] + α · (x[n]² − msq[n−1])       α = 1 − exp(−T/τ)
   L[n]   = 10 · log10(msq[n])

   Fast:  τ = 0,125 s  →  α = 1,667·10⁻⁴  przy 48 kHz
   Slow:  τ = 1,0   s  →  α = 2,083·10⁻⁵  przy 48 kHz
```

**Zostajemy przy Fast**, mimo że dla „hałasu utrzymanego" Slow wydaje się naturalniejszy.
Dwa powody: (1) spójność z pierwszym urządzeniem — to był Twój warunek, (2) wygładzanie
i tak zrobi za nas erozja z § 5, a robi to w sposób, który da się opisać jednym zdaniem.
Uśrednianie dwa razy to gubienie informacji bez zysku.

> ⚠️ **Uśredniaj MOC, nie decybele.** Dla sygnału skaczącego między 40 a 80 dB:
> poprawnie `10·log10((10⁴+10⁸)/2) = 77,0 dB`, błędnie `(40+80)/2 = 60,0 dB`.
> Siedemnaście decybeli. Motocykl na przepustnicy to dokładnie taki sygnał.

### 4.3 Ze skali cyfrowej na SPL

```
   dB(A) = dBFS + K
```

Jedna liczba, jedno dodawanie, **żadnego mnożnika.** Jeśli po kalibracji nachylenie
nie wychodzi 1,0, to jest **diagnostyka sprzętu, a nie parametr do dopasowania** —
szukaj ALC, bramki szumów albo przesterowania. `slope = 1,36` z Raspberry Pi wziął
się dokładnie z ulegnięcia tej pokusie.

---

## 5. Nowa metryka — maksimum utrzymane co najmniej 5 s

To jest sedno tego urządzenia, więc rozpiszmy je dokładnie.

### 5.1 Czego chcemy, a czego nie

Chcesz odsiać piki: uderzenie kamienia w owiewkę, trzask zawieszenia, klaśnięcie
przy zmianie biegu. Chcesz zostawić to, co utrzymało się przez 5 sekund — czyli
motocykl faktycznie pracujący na obrotach.

### 5.2 Dlaczego uśrednianie po 5 s NIE działa

Naturalny pierwszy pomysł to `LAeq` liczone w ruchomym oknie 5 s. Policzmy, co robi
z pojedynczym pikiem 120 dB trwającym 100 ms na tle 50 dB:

```
   LAeq,5s = 10·log10( 10^12 · 0,1/5 )  =  10·log10(2·10^10)  =  103,0 dB
```

**Pik przeżył.** Stracił 17 dB, ale nadal wygrywa z każdym realnym warkotem silnika.
Uśrednianie rozmazuje impuls, zamiast go odrzucić — bo energia nie znika od tego,
że podzielisz ją przez dłuższy czas. To ta sama pułapka co przy `slope`: pozornie
rozsądna operacja, która nie robi tego, co się wydaje.

### 5.3 Co działa: minimum ruchome (erozja)

Definicja, którą proponuję:

> **L_sus(5 s)** to najwyższy poziom L taki, że istnieje 5‑sekundowy odcinek,
> w którym poziom LAF był **przez cały czas** równy L lub wyższy.

Czyli: przesuwasz po przebiegu LAF okno 5 s, w każdym położeniu bierzesz **minimum**,
a na koniec **maksimum z tych minimów**.

```
    LAF
     │        ▲ pik 120 dB, 100 ms
     │        │
 100 ├────────┼──────────────────────────────
     │        │           ┌──────────────┐
  85 ├────────┼───────────┤ silnik 6 s   ├───
     │        │           │              │
  50 ├────────┴───────────┘              └───   tło
     └──────────────────────────────────────► t

    minimum w oknie 5 s obejmującym pik  →  50 dB   (pik odrzucony)
    minimum w oknie 5 s wewnątrz warkotu →  85 dB   (warkot zachowany)
    L_sus(5 s) = 85 dB                              ✅ dokładnie o to chodziło
```

Pik **znika całkowicie**, a nie „zostaje osłabiony o 17 dB". Warunek jest jakościowy,
nie ilościowy: albo hałas trwał 5 s, albo nie.

Metryka ma też własność, która czyni ją odporną na spór: jest **monotoniczna**.

```
   L_sus(5 s)  ≤  L_sus(1 s)  ≤  LAFmax
```

Zawsze. To dobry test jednostkowy i dobry argument w rozmowie — L_sus nigdy nie może
wyjść wyżej niż zwykłe maksimum, więc nie da się zarzucić, że zawyża.

### 5.4 Poprawka konieczna: tolerancja na zapady

Czyste minimum jest **zbyt surowe**. Jedna próbka niska zabija całe okno, a mamy
dwa mechanizmy, które takie zapady produkują:

**(a) Motocykl faluje.** Poziom przy pracy na obrotach nie jest linią prostą.

**(b) Sam filtr LAF potrzebuje czasu na narastanie.** Przy τ = 125 ms dojście do
0,5 dB od wartości docelowej zajmuje `2,22·τ ≈ 0,28 s`. Dźwięk trwający **dokładnie**
5,0 s da więc pełny poziom tylko przez ~4,7 s — i czyste minimum go odrzuci.
Metryka „≥ 5 s" nie wykryłaby zdarzenia trwającego 5 s. To nie do przyjęcia.

**Rozwiązanie:** zamiast minimum bierz **niski percentyl okna**. Zamiast „przez cały
czas" — „przez co najmniej p % czasu":

| p | znaczenie | tolerowany zapad w oknie 5 s |
|---|---|---|
| 100 % | czyste minimum | 0 s — odrzuca własne narastanie filtru |
| **90 %** | **zalecane** | **0,5 s** — mieści narastanie (0,28 s) z zapasem |
| 80 % | luźne | 1,0 s — zacznie przepuszczać serie pików |

Przy `p = 90 %` liczba zachowuje sens: *„przez 5 sekund, z tolerancją pół sekundy,
poziom nie spadł poniżej L"*. Zwróć uwagę, że to jest **dokładnie ta sama operacja,
co LA90** z [StatsWindow](../lib/meter/StatsWindow.h) — tylko liczona w oknie 5 s
zamiast w minucie. Nie wprowadzamy nowego pojęcia, tylko stosujemy stare w innej skali.

### 5.5 Implementacja — histogram ruchomy

Percentyl w ruchomym oknie liczy się histogramem, który dodaje nową próbkę
i usuwa najstarszą. Koszt stały, bez alokacji, bez sortowania.

Budżet przy oknie 5 s i kroku 10 ms → **500 próbek**:

| | rozmiar |
|---|---|
| bufor cykliczny indeksów koszyków (`uint16 × 500`) | 1 000 B |
| histogram (`uint16 × 300` koszyków) | 600 B |
| **razem** | **1,6 kB** |

Liczenie percentyla to skan 300 koszyków co 10 ms — 30 tys. operacji na sekundę
przy 240 MHz. Niezauważalne.

Proponowany plik: **`lib/meter/SustainedLevel.h`** (obok istniejących klas — ta sama
konwencja: zero wiedzy o ESP32, testowalne na Macu).

```cpp
// ============================================================================
//  SustainedLevel — maksimum poziomu, który UTRZYMAŁ SIĘ zadany czas.
//
//  Odpowiada na pytanie: „jak głośno było przez co najmniej 5 sekund?",
//  a nie „jak głośno było w najgłośniejszej chwili?".
//
//  Piki są odrzucane JAKOŚCIOWO, a nie tłumione. Uśrednianie po 5 s
//  zostawiłoby z piku 120 dB / 100 ms aż 103 dB — patrz docs/MOTOCYKL.md § 5.2.
//
//  Zakres i koszyki histogramu SĄ TE SAME co w StatsWindow — celowo,
//  żeby obie metryki miały identyczną rozdzielczość i zaokrąglenia.
// ============================================================================

#pragma once

#include <cstddef>
#include <cstdint>

#include "StatsWindow.h"

namespace meter {

class SustainedLevel {
public:
    /// Maksymalne okno, jakie klasa obsłuży (5 s przy kroku 10 ms).
    static constexpr size_t kMaxSamples = 500;

    /// Te same koszyki co StatsWindow — 0,5 dB, od −140 do +10 dB.
    static constexpr size_t kBins = StatsWindow::kBins;

    /**
     * @param windowSec        ile sekund hałas musi trwać
     * @param stepMs           co ile ms dostajemy odczyt LAF (10 ms = jak histogram)
     * @param tolerancePercent przez jaki % okna poziom ma być utrzymany
     *                         (100 = czyste minimum, 90 = zalecane)
     */
    explicit SustainedLevel(float windowSec = 5.0f,
                            float stepMs = 10.0f,
                            float tolerancePercent = 90.0f);

    void configure(float windowSec, float stepMs, float tolerancePercent);

    /// Pełne zerowanie — okno i zapamiętane maksimum.
    void reset();

    /// Zeruje wyłącznie maksimum. ⚠️ NIE zeruje okna: hałas trwający
    /// przez granicę raportu musi być widziany w całości, dokładnie tak,
    /// jak filtr A nie jest zerowany między oknami.
    void resetMax();

    /// Odczyt poziomu LAF w dBFS. Wołany co `stepMs`.
    void addLevel(float levelDb);

    /// Najwyższy utrzymany poziom od ostatniego resetMax(), w dBFS.
    /// kNoData, dopóki nie uzbierało się pełne okno.
    float sustainedDb() const { return maxDb_; }

    /// Poziom utrzymany w BIEŻĄCYM oknie — do podglądu na żywo.
    float currentDb() const;

    /// Czy okno zdążyło się wypełnić (przed tym wynik jest bez znaczenia).
    bool ready() const { return filled_ >= capacity_; }

    static constexpr float kNoData = StatsWindow::kNoData;

private:
    size_t binFor(float levelDb) const;
    float  percentileDb() const;

    uint16_t ring_[kMaxSamples] = {};   // indeksy koszyków, cyklicznie
    uint16_t bins_[kBins]       = {};   // ile próbek w każdym koszyku
    size_t   capacity_ = kMaxSamples;
    size_t   head_     = 0;
    size_t   filled_   = 0;
    float    tolerance_ = 90.0f;
    float    maxDb_     = kNoData;
};

}  // namespace meter
```

Rdzeń implementacji:

```cpp
void SustainedLevel::addLevel(float levelDb) {
    const size_t idx = binFor(levelDb);

    if (filled_ == capacity_) {
        --bins_[ring_[head_]];          // okno pełne — najstarsza wypada
    } else {
        ++filled_;
    }
    ring_[head_] = static_cast<uint16_t>(idx);
    head_ = (head_ + 1) % capacity_;
    ++bins_[idx];

    if (filled_ < capacity_) return;    // niepełne okno nic nie orzeka

    const float poziom = percentileDb();
    if (maxDb_ == kNoData || poziom > maxDb_) maxDb_ = poziom;
}

float SustainedLevel::percentileDb() const {
    // Idziemy od NAJGŁOŚNIEJSZYCH koszyków w dół, aż uzbieramy `tolerance_`
    // procent okna. Poziom, na którym się zatrzymamy, jest tym utrzymanym.
    // Kierunek jest ten sam co w StatsWindow::exceededDb — 90 % daje wartość
    // NISKĄ, bo pytamy „jaki poziom był przekroczony przez 90 % czasu".
    const double cel = static_cast<double>(capacity_) * tolerance_ / 100.0;

    double suma = 0.0;
    for (size_t i = kBins; i-- > 0; ) {
        suma += bins_[i];
        if (suma >= cel) {
            return StatsWindow::kMinDb +
                   (static_cast<float>(i) + 0.5f) * StatsWindow::kBinWidthDb;
        }
    }
    return StatsWindow::kMinDb;
}
```

Podpięcie w `LevelMeter::processBlockNormalized` — jedna linia obok istniejącego
`stats_.addLevel()`:

```cpp
if (++histogramCounter_ >= histogramDivider_) {
    histogramCounter_ = 0;
    const float db = TimeWeighting::toDb(ms);
    stats_.addLevel(db);
    sustained_.addLevel(db);      // ◄── nowe
}
```

I odczyt w dB SPL, przez ten sam `offset()` co reszta:

```cpp
float lsus() const { return offset(sustained_.sustainedDb()); }
```

### 5.6 Testy, które muszą przejść

Do `test/test_sustained/`, w konwencji istniejących testów:

| Test | Wejście | Oczekiwanie |
|---|---|---|
| `pik_jest_odrzucany` | tło 50 dB + pik 120 dB przez 100 ms | L_sus ≈ **50 dB**, nie 103 |
| `warkot_jest_zachowany` | 6 s ciągłego tonu 85 dB | L_sus ≈ **85 dB** (±0,5) |
| `za_krotki_nie_liczy_sie` | 3 s tonu 85 dB na tle 50 dB | L_sus ≈ **50 dB** |
| `dokladnie_piec_sekund` | 5,0 s tonu 85 dB | L_sus ≈ **85 dB** — pilnuje tolerancji z § 5.4 |
| `seria_pikow_to_nie_warkot` | 10 pików co 0,5 s | L_sus = poziom tła |
| `monotonicznosc` | dowolny sygnał | `L_sus(5s) ≤ L_sus(1s) ≤ LAFmax` |

Czwarty jest najważniejszy — to on złapie regresję, gdyby ktoś „uprościł" tolerancję
z powrotem do czystego minimum.

---

## 6. Warunki, w których pomiar jest wiarygodny

Motocykl w ruchu to znacznie trudniejsze środowisko niż parapet. Trzy zjawiska
potrafią zamienić pomiar w fikcję i **wszystkie trzy trzeba oznaczać w danych**,
a nie po cichu poprawiać.

### 6.1 Wiatr — największe zagrożenie

Turbulencja na membranie mikrofonu daje ciśnienie, które przetwornik rejestruje
jako bardzo głośny hałas, choć nikt go nie słyszy. Przy prędkościach autostradowych
**bez osłony przeciwwietrznej pomiar jest bezwartościowy** — nie „obarczony błędem",
tylko bezwartościowy.

Trzy warstwy obrony, w kolejności ważności:

1. **Osłona mechaniczna.** Gąbka to minimum, futerko („dead cat") jest wyraźnie
   lepsze. Mikrofon nie może patrzeć w strumień powietrza — schowaj go za owiewką
   albo skieruj w dół.
2. **Detektor wiatru w oprogramowaniu.** Energia wiatru siedzi bardzo nisko —
   poniżej ~200 Hz. Policz stosunek energii **przed** ważeniem A (pasmo niskie)
   do energii **po** ważeniu A. Przy wietrze ten stosunek gwałtownie rośnie, bo
   filtr A wycina bas. Prosty próg na tej wielkości daje flagę `wind`.
3. **Bramka prędkości z GPS.** Powyżej progu (do ustalenia pomiarem — zacznij od
   50 km/h) oznaczaj rekordy jako niepewne.

> ⚠️ **Flaguj, nie odrzucaj.** Rekord z podniesioną flagą `wind` nadal ma wartość —
> mówi, że w tym miejscu nie da się nic orzec. Cicho wyrzucony rekord to dziura
> w danych, której nikt nie zauważy. To ta sama zasada, co `clampedCount_`
> w StatsWindow: przyklejenie do skrajnego koszyka jest liczone, a nie przemilczane.

### 6.2 Wibracje

Mikrofon MEMS reaguje na drgania konstrukcji. Montaż **na gumie**, nie na sztywno
do ramy. BMI270 jest na płytce za darmo — zapisuj RMS przyspieszenia w rekordzie
i będziesz mógł po fakcie sprawdzić korelację z podejrzanymi odczytami. Nie próbuj
tego kompensować w locie; najpierw zbierz dane.

### 6.3 Zapas przed przesterowaniem — tu decyzja jest ODWROTNA niż w domu

W DBMeterV1 walczyliśmy o **niską podłogę**, bo nocne tło potrafi zejść do 30 dB.
W motocyklu podłoga jest **kompletnie nieistotna** — nic ciszej niż 60 dB tam nie
wystąpi. Za to u góry: wydech z bliska to **100–110 dB(A)**, a przy nieszczelnym
tłumiku więcej.

Przy obecnym ustawieniu (suma wzmocnienia 48 dB, `K = 121,6`) pełna skala leży przy
**121,6 dB(A)** — zapas raptem kilkanaście dB, a przesterowany pomiar jest po prostu
nieprawdziwy i zaniżony.

Pomiar z 30.08 zapisany w [config.h](../include/config.h) mówi, że podłoga rośnie
**dokładnie 1:1 ze wzmocnieniem cyfrowym**, czyli szum powstaje na wejściu przetwornika,
a nie w kwantyzacji 16‑bit. Wynika z tego rzecz bardzo wygodna:

> Zmniejszenie wzmocnienia **cyfrowego** o 12 dB (`kAdcScaleStep` z 7 na 5) podnosi
> sufit do ~133 dB(A), a podłogę w dB(A) zostawia **mniej więcej tam, gdzie była** —
> bo K spada o tyle samo, o ile rośnie dBFS szumu. Zapas dostajesz niemal za darmo.

⚠️ To wynika z modelu, nie z bezpośredniego pomiaru w tym zakresie — **zweryfikuj
komendą `szum` po zmianie**, zanim się na tym oprzesz. Zachowaj PGA na 6 dB: analogowe
wzmocnienie przed przetwornikiem jest tym, które realnie poprawia stosunek sygnału
do szumu, a przy 6 dB nasycenia nie ma.

I tak czy inaczej: **licznik przesterowań musi jechać w każdym rekordzie.** Niezerowy
= wynik raportujesz jako „≥ X dB", nigdy jako „X dB".

---

## 7. Zbieranie lokalne i wysyłka po jeździe

StickS3 nie ma karty SD, ale ma 8 MB Flash — czyli LittleFS. Policzmy, czy wystarczy.

Rekord o **stałym rozmiarze 24 B**, zapisywany raz na sekundę:

| pole | typ | uwaga |
|---|---|---|
| `t` | `uint32` | sekundy od startu sesji (czas bezwzględny w nagłówku sesji) |
| `laeq` | `int16` | w 0,1 dB |
| `lafmax` | `int16` | w 0,1 dB |
| **`lsus5`** | `int16` | **w 0,1 dB — nowa metryka** |
| `la90` | `int16` | tło, w 0,1 dB |
| `lat` | `int32` | stopnie × 10⁷ |
| `lon` | `int32` | stopnie × 10⁷ |
| `speed` | `uint16` | cm/s |
| `flags` | `uint8` | wiatr / przesterowanie / brak fixa / wibracje |
| `sats` | `uint8` | liczba satelitów |

```
   1 s   →     24 B
   1 h   →     86 kB
  10 h   →    864 kB
```

Przy partycji LittleFS rzędu 2 MB masz **ponad 20 godzin jazdy**. Zapas jest taki,
że nie warto komplikować formatu — stały rozmiar rekordu daje ci za darmo bufor
cykliczny, wznowienie po restarcie i liczenie pozycji arytmetyką zamiast parsowaniem.

**Kolejność, która chroni dane:**

1. Zapis lokalny jest **pierwszy i bezwarunkowy**. Pomiar nigdy nie czeka na sieć.
2. Wysyłka dopiero po jeździe, gdy pojawi się znane WiFi.
3. Rekord kasujesz **dopiero po potwierdzeniu 200** z serwera.
4. Watchdog restartuje układ po 60 s bez oznak życia (jak w DBMeterV1) — a po
   restarcie sesja jest wznawiana, nie zaczynana od zera.

Po stronie serwera warto dodać `lsus5` jako osobne pole, obok istniejących. Protokół
z [API.md](API.md) przenosi się w całości — dochodzi jedna kolumna i pola GPS.

---

## 8. Kalibracja tego egzemplarza

Procedura z [KALIBRACJA.md](KALIBRACJA.md) obowiązuje **bez zmian** — z trzema
uzupełnieniami wynikającymi z zastosowania.

**Kolejność jest sztywna:**

```
   1. wzmocnienie   ──►   2. kalibracja K   ──►   3. montaż   ──►   4. K raz jeszcze
```

Krok 4 nie jest formalnością. Skoro sam obrót przyrządu dawał 7 dB, to zamknięcie
w uchwycie na motocyklu unieważnia kalibrację ze stołu. **Kalibruj urządzenie
w docelowym montażu**, albo przynajmniej powtórz weryfikację po zamontowaniu.

**Uzupełnienia dla motocykla:**

- **Punkty kalibracyjne wyżej.** Tabela w KALIBRACJA.md § 5 kończy się na ~90 dB.
  Tutaj dołóż punkty w okolicach 95–100 dB, bo tam faktycznie będziesz mierzył.
  Ekstrapolacja poza zakres kalibracji to zgadywanie.
- **Kalibruj na szumie ustalonym, nigdy na motocyklu.** Ta sama zasada, co „nigdy
  nie kalibruj na szczekaniu", i z tego samego powodu: GM1351 odświeża odczyt
  2 razy na sekundę. Silnik na stałych obrotach jest akurat dobrym sygnałem
  **weryfikacyjnym** — ale wzorcem jest szum różowy z `tools/szum_rozowy.py`.
- **Sprawdź L_sus na czymś, co znasz.** Puść ton 5 s i ton 3 s przy znanym poziomie.
  Pierwszy ma dać poziom, drugi tło. To weryfikacja całej ścieżki naraz — od
  mikrofonu po nową metrykę — i zajmuje minutę.

Docelowo: `K` osobno dla każdego egzemplarza, przysyłane z serwera. Zwróć uwagę na
pułapkę z config.h — do bazy trafiła kiedyś wartość z urządzenia testowego i jej
przyjęcie przesunęłoby pomiary o 32 dB. **Najpierw poprawna wartość na serwerze,
potem włączenie kanału.**

---

## 9. Plan wdrożenia

Etapami, każdy z własnym kryterium zaliczenia — bo diagnozowanie trzech nowych
rzeczy naraz kończy się zawsze tak samo.

| Etap | Zakres | Kryterium |
|---|---|---|
| **A** | Port `AudioInput` na piny StickS3 | `begin()` wykrywa kanał, energia niezerowa |
| **B** | `LevelMeter` bez zmian, odczyt na LCD | `szum` i `poziom` dają sensowne liczby |
| **C** | Wzmocnienie + `K` na stole | nachylenie ≈ 1,0 na 5 punktach do 100 dB |
| **D** | `SustainedLevel` + testy `native` | wszystkie testy z § 5.6 zielone |
| **E** | GPS (NMEA po Grove) | pozycja i prędkość w logu, brak fixa nie psuje pomiaru |
| **F** | Zapis LittleFS | godzina pracy, restart, dane przetrwały |
| **G** | Wysyłka po jeździe | rekord znika lokalnie dopiero po 200 |
| **H** | Montaż + rekalibracja + osłona | L_sus stabilne na postoju na obrotach |

Etapy A–D robisz **bez motocykla**, a D w całości na Macu (`pio test -e native`).
To jest ta sama zaleta, którą dał podział w pierwszym projekcie: logika pomiarowa
nie wie nic o ESP32 i można ją dopracować przy biurku.

---

## 10. Rzeczy, których świadomie tu nie ma

- **Rozpoznawania źródła dźwięku.** Sam o to prosiłeś i to dobra decyzja — cała
  warstwa `NoiseCharacter` / detektora szczekania zostaje w DBMeterV1. Miernik ma
  mierzyć hałas, a nie zgadywać jego pochodzenie.
- **Wartości progu wiatru i progu prędkości.** Nie zmyślę ich — wychodzą z pierwszego
  przejazdu z surowym logiem. Zbierz dane, popatrz na rozkład, dopiero potem próg.
- **Wyboru czasu 5 s jako czegoś normatywnego.** To Twoje kryterium, nie norma.
  Dlatego `windowSec` jest parametrem, a nie stałą — gdyby okazało się, że 3 s lepiej
  łapie to, o co Ci chodzi, zmiana kosztuje jedną liczbę.

---

## Skąd wzięte

- Tor pomiarowy, przeliczniki i pułapki: [ARCHITEKTURA.md](ARCHITEKTURA.md),
  [KALIBRACJA.md](KALIBRACJA.md), [config.h](../include/config.h),
  [AudioInput.cpp](../src/AudioInput.cpp), [lib/meter/](../lib/meter/)
- Specyfikacja i piny StickS3: [docs.m5stack.com/en/core/StickS3](https://docs.m5stack.com/en/core/StickS3)
  — ⚠️ piny I2S zweryfikuj pierwszym uruchomieniem, dokumentacja M5Stack już raz nas
  wprowadziła w błąd nazewnictwem z perspektywy kodeka.
