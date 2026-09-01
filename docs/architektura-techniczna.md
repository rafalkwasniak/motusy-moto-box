# Motusy Moto Box — Architektura techniczna

Dokument towarzyszący [specyfikacji funkcjonalnej](motusy-moto-box.md). Specyfikacja mówi
**co** urządzenie ma robić — ten dokument mówi **jak** i **dlaczego tak, a nie inaczej**.

Status: wersja robocza, przed testami na sprzęcie.
Data: 2026-08-27

---

## 1. Platforma sprzętowa — potwierdzone parametry

M5Stack **M5StickS3** (K150).

| Element | Szczegóły | Uwagi dla firmware |
|---|---|---|
| MCU | ESP32-S3-PICO-1 (LGA56) rev v0.2 | ✅ odczytane z krzemu 2026-08-28 |
| Flash | 8 MB wbudowany (GD), tryb **quad**, 3.3 V | ✅ potwierdza `flash_mode: qio` |
| PSRAM | 8 MB wbudowany (AP_3v3) | ✅ potwierdza `memory_type: qio_opi` |
| Kwarc | 40 MHz | |
| MAC | 70:04:1d:dc:6b:c8 | jedyna sztuka, port `/dev/cu.usbmodem1101` |
| Ekran | ST7789P3, 135×240 IPS | podświetlenie GPIO38, PWM LEDC ch7 @256 Hz |
| IMU | BMI270, I²C @0x68/0x69 | magistrala wewnętrzna: SDA=47, SCL=48 |
| PMIC | **M5PM1 (PY32)** @0x6E | *nie* AXP192/AXP2101 — inne API niż starsze Sticki |
| Audio | ES8311 @0x18 + wzmacniacz AW8737 | I²S: MCK=18, BCK=17, WS=15, DOUT=14, DIN=16 |
| | | amp enable = GPIO3 **PMIC-a**, nie ESP32 |
| Przyciski | KEY1=G11, KEY2=G12, PWR/RST=G0 | **dwa** przyciski użytkownika |
| Bateria | 250 mAh | patrz budżet energetyczny, §4 |
| Rozszerzenia | Grove Port A: G9/G10; Hat2-Bus 16P | wolne na przyszłość (GPS w v2) |

Biblioteki: **M5Unified 0.2.21** + **M5GFX 0.2.28** (wsparcie StickS3 od 0.2.12).

Toolchain: PlatformIO `espressif32@7.0.1`, który ciągnie **Arduino core 2.0.17**
(ESP-IDF 4.4). Firmware kompiluje się i linkuje poprawnie. Gdyby okazało się, że
tryb deep sleep albo M5PM1 wymagają nowszego IDF, alternatywą jest fork
`pioarduino/platform-espressif32` z Arduino core 3.x — na razie nie ma powodu.

### 1.1. Do zweryfikowania na fizycznym sprzęcie

Lista rzeczy, których nie da się rozstrzygnąć z dokumentacji. Każda ma przypisany
plan awaryjny, żeby brak potwierdzenia nie blokował projektu.

| # | Pytanie | Plan B jeśli „nie" | Stan |
|---|---|---|---|
| V1 | Czy da się odróżnić „VBUS obecny" od „bateria naładowana"? (§3) | histereza na napięciu baterii | ✅ **TAK — PMIC mierzy napięcie wejściowe** |
| V2 | Czy piny INT BMI270 są wyprowadzone do GPIO ESP32? | wybudzanie timerem PMIC co ~2 s | otwarte |
| V3 | Czy jest RTC PCF8563 @0x51? (źródła sprzeczne) | nieistotne dla MVP | ✅ **RTC NIE MA** |
| V4 | Realny pobór prądu w deep sleep z aktywnym IMU | korekta czasu czuwania w §4 | otwarte |
| V5 | Zakres i ODR osiągalne na BMI270 przez M5Unified | bezpośredni dostęp do rejestrów | otwarte |

### 1.2. Wynik pierwszego uruchomienia (2026-08-28)

Skan magistrali z fizycznego urządzenia:

```
I2C (SDA=47 SCL=48): 3 uklady
  0x18  ES8311    OK
  0x51  PCF8563   brak        <- V3 rozstrzygniete: RTC NIE MA
  0x68  BMI270    OK          <- adres podstawowy, nie 0x69
  0x69  BMI270-B  brak
  0x6E  M5PM1     OK
```

Potwierdzone dodatkowo:

- **PSRAM działa** — 8 276 807 B wolnego, czyli `memory_type: qio_opi` jest poprawne
  i bufor ekranu faktycznie siedzi w PSRAM.
- **M5GFX rozpoznaje płytkę sam** — `[Autodetect] board_M5StickS3` (board:26).
  Nie musimy niczego wymuszać flagami.
- Sklep miał rację co do RTC, dokumentacja społecznościowa się myliła. Dla MVP
  bez znaczenia, ale w v2 (znaczniki czasu przejazdów) trzeba będzie liczyć czas
  inaczej albo brać go z GPS.

**V1 ROZSTRZYGNIĘTE (2026-08-29).** Droga do odpowiedzi miała trzy etapy i warto
ją zapamiętać, bo pokazuje, jak łatwo zbudować skomplikowane obejście problemu,
który ma proste rozwiązanie:

1. Założenie, że `isCharging()` znaczy „jest prąd" — **błędne**. Przy pełnej
   baterii funkcja **migocze**, bo ładowarka cyklicznie kończy i wznawia
   doładowywanie. Naiwna implementacja uzbroiłaby alarm w trakcie jazdy.
2. Obejście: traktowanie `isCharging()` jak tętna — zasilanie obecne, jeśli
   impuls był w ciągu 15 s. Działało, ale kosztowało ~17 s opóźnienia detekcji
   i wymagało antydatowania odliczania.
3. **Właściwe rozwiązanie:** PMIC M5PM1 ma rejestr z napięciem wejściowym,
   dostępny przez `M5.Power.M5pm1.getVBUSVoltage()`. Zmierzone: **5,21 V
   z kablem, 0 V bez**. Jednoznacznie i natychmiast.

`PowerSource` używa teraz napięcia jako sygnału podstawowego (progi 4,0 V / 3,0 V
z histerezą), a mechanizm tętna został fallbackiem na wypadek płytki, która tego
rejestru nie oddaje. Potwierdzenie czasowe zaniku (2 s) zostaje — chroni przed
zapadem napięcia przy rozruchu silnika.

---

## 2. Pomiar przechyłu — fizyka i konsekwencje

### 2.1. Problem

**Akcelerometr nie mierzy przechyłu motocykla w zakręcie.**

Motocykl w ustalonym zakręcie jest w równowadze: wypadkowa siły grawitacji i siły
odśrodkowej działa wzdłuż pionowej osi motocykla. Akcelerometr zamontowany sztywno
do ramy zmierzy tę wypadkową jako „dół" — czyli pokaże przechył ≈ 0°, niezależnie
od tego, czy motocykl jest pionowo, czy położony na 45°.

```
     motocykl pionowo            motocykl w zakręcie 40°
                                          
          │                            ╲
          │  ↓ 1.00 g                   ╲  ↓ 1.31 g
          │                              ╲
     ─────┴─────                    ──────╲──────
                                          
  akcel: przechył 0°            akcel: przechył 0°  ← błędnie!
```

Akcelerometr mierzy przechył poprawnie **tylko na postoju** i w stanach nieustalonych
(moment przechylania), nie w ustalonym zakręcie.

### 2.2. Rozwiązanie przyjęte w MVP

Przechył pochodzi z **całkowania prędkości kątowej żyroskopu** wokół osi wzdłużnej
motocykla. Żyroskop mierzy prawdziwą prędkość przechylania niezależnie od sił.

Problemem całkowania jest **dryft** — akumulacja błędu offsetu żyroskopu. Zwalczamy go
trzema mechanizmami:

1. **Estymacja biasu w spoczynku.** Gdy urządzenie jest nieruchome (mały moduł żyra,
   moduł akcelerometru ≈ 1 g), uśredniamy odczyt żyra jako bieżący offset.
2. **Bramkowana korekcja akcelerometrem.** Kąt z akcelerometru dociągamy do estymaty
   **wyłącznie** wtedy, gdy odczyt jest wiarygodny: `|‖a‖ − 1g| < tol` przy jednocześnie
   małych prędkościach kątowych. W zakręcie ten warunek jest niespełniony, więc
   filtr nie „prostuje" motocykla — to jest sedno całego rozwiązania.
3. **Powolny zanik do zera** przy długotrwałej jeździe na wprost.

Klasyczny filtr komplementarny ze stałym wzmocnieniem tutaj **nie zadziała** — dlatego
warunek bramkowania jest jawnym, strojonym parametrem, a nie szczegółem implementacji.

### 2.3. Realistyczna dokładność

**±3–5°**, nie ±0.5°. W konsekwencji:

- wartości przechyłu prezentujemy jako **liczby całkowite** (`LEWO 32°`, nie `32.4°`);
- specyfikację §8.1/§8.2 traktujemy jako przykład formatu, nie deklarację precyzji.

### 2.4. Prędkość jako drugie, niezależne źródło przechyłu

Mając prędkość, przechył liczy się wprost z równowagi w ustalonym zakręcie:

```
tan(φ) = v · ω / g
```

gdzie `ω` to prędkość odchylania wokół **pionu świata** (nie wokół osi motocykla —
przechylony motocykl skręca mieszanką prędkości `q` i `r`).

To jest pomiar **absolutny**, nie całkowany, więc nie dryfuje — i co najważniejsze,
**działa dokładnie tam, gdzie bramka akcelerometru jest zamknięta**: w zakręcie.
Oba źródła korekcji uzupełniają się idealnie:

| Sytuacja | Akcelerometr | Prędkość + żyro |
|---|---|---|
| Postój, jazda na wprost | ✅ działa | ❌ (ω ≈ 0) |
| Ustalony zakręt | ❌ pokazuje 0° | ✅ działa |
| Wejście/wyjście z zakrętu | ❌ | ⚠️ tylko przybliżenie |

Filtr ma tę korekcję **zaimplementowaną od początku** (`Orientation::setSpeedHint`).
Bez źródła prędkości pozostaje po prostu nieaktywna — dołożenie GPS to podanie
prędkości do gotowego wejścia, bez zmian w algorytmie.

### 2.5. Moduł GPS — M5Stack Unit GPS/BDS (AT6668)

| Parametr | Wartość | Znaczenie dla projektu |
|---|---|---|
| Chipset | AT6668 + MAX2659 | GPS, BDS, GLONASS, GALILEO, QZSS |
| Interfejs | UART, NMEA 0183 4.1 | Grove Port A (G9/G10) — **wolny** |
| Odświeżanie | 1–10 Hz | prędkość wolniejsza niż IMU (100 Hz) — stąd korekcja, nie zastąpienie |
| Dokładność pozycji | 1,5 m | prędkość dopplerowska jest dokładniejsza niż z różniczkowania pozycji |
| Pobór | ~32 mA @5 V | **tylko w trybie jazdy**; w trybie alarmowym musi być odcięty |
| Zimny start | 28 s | ekran startowy 5 s nie wystarczy — filtr musi działać bez fixa |
| Wymiary | 48 × 24 × 8 mm | dokładnie footprint Sticka |

Wymagania wynikające z powyższego:

- **Degradacja bez fixa jest obowiązkowa.** Tunel, garaż, pierwsze 30 s po starcie —
  urządzenie działa dalej na samym żyroskopie, tylko z gorszą korekcją dryftu.
  `speedHintMaxAgeMs` pilnuje, żeby stary odczyt nie był używany jako świeży.
- **Zasilanie GPS musi być odcinane** przed wejściem w deep sleep, inaczej 32 mA
  zabije baterię w ~8 godzin i alarm nie doczeka rana.
- Port A zajęty — kolejne moduły tylko przez Hat2-Bus.

---

## 3. Przyspieszenie i hamowanie — kompensacja pochylenia

Ten sam rodzaj problemu, mniejsza skala.

Akcelerometr w osi jazdy miesza rzeczywiste przyspieszenie ze składową grawitacji
przy zmianie pochylenia:

- nurkowanie zawieszenia przy hamowaniu → zawyża hamowanie,
- przysiad przy przyspieszaniu → zawyża przyspieszenie,
- **podjazd 10% zapisze się jako hamowanie −0.10 g** przy stałej prędkości.

Kompensacja: `a_wzdłużne = a_x − g·sin(pitch)`, gdzie `pitch` pochodzi z tej samej
fuzji czujników co przechył. Wynik dodatkowo filtrujemy dolnoprzepustowo, żeby
pojedyncze uderzenie na dziurze nie ustanowiło fałszywego rekordu.

**Wartość maksymalna musi się utrzymać przez minimalny czas** (rzędu 100–200 ms), żeby
zostać zapisana jako rekord. Szczegóły progów — po analizie danych z jazdy.

---

## 3a. Prędkość maksymalna — piąty parametr

Dodana na etapie projektowania, przed powstaniem schematu NVS — dzięki czemu
nie wymagała migracji danych.

### Dlaczego pasuje do tego zestawu

Prędkość maksymalna jest tym samym rodzajem wartości co pozostałe cztery: rekordem
z sesji, aktualizowanym przez proste „czy większe niż dotychczas". Wchodzi więc
w istniejącą strukturę `RideValues` i podlega tym samym regułom co reszta:
zerowana przy nowej sesji w OSTATNIA JAZDA, nietykalna w MAX OGÓLNIE.

### Czym różni się od pozostałych czterech

**Jest znacznie dokładniejsza.** Przechył ma niepewność 3–5°, bo pochodzi
z całkowania. Prędkość z GPS to pomiar bezpośredni (dopplerowski), z dokładnością
lepszą niż 1 km/h. To jedyna wartość na ekranie, której można ufać co do jednostki.

**Ma stan „brak danych".** Pozostałe cztery zawsze coś pokazują. Prędkość bez modułu
GPS nie ma czego pokazać — i `0 km/h` byłoby kłamstwem, bo wygląda jak wynik pomiaru.
Dlatego wiersz wyświetla `---`, dopóki nie ma fixa.

### Filtrowanie

| Sytuacja | Reakcja |
|---|---|
| Brak fixa | próbka odrzucona (`SpeedSample::valid == false`) |
| Szum na postoju (2–3 km/h) | odrzucone progiem `minSpeedKmh = 5` |
| Błędny fix po tunelu (np. 1200 km/h) | odrzucone progiem `maxSpeedKmh = 400` |

Ocena jakości fixa (liczba satelitów, HDOP) należy do warstwy parsującej NMEA —
`RideMetrics` dostaje już gotową flagę `valid` i nie zgaduje.

### Wpływ na specyfikację funkcjonalną

- **§8** — dochodzi piąty rejestrowany parametr: prędkość maksymalna.
- **§9** — zestawy mają po 5 wartości, ekran prezentuje 10, nie 8.
- **§13** — pamięć nieulotna przechowuje po 5 wartości na zestaw.
- **§24** — układ ekranu ma pięć wierszy.
- **§29** — GPS przestaje być „poza zakresem"; staje się opcjonalnym modułem v1.

---

## 4. Budżet energetyczny i tryb alarmowy

### 4.1. Problem

Bateria ma **250 mAh**. Przy włączonym ekranie StickS3 pobiera kilkadziesiąt mA —
czuwanie rzędu **3–6 godzin**. Alarm ma pilnować motocykla przez noc albo przez
tydzień pod hotelem. Zapis §18 specyfikacji („ekran może nadal prezentować wyniki")
jest w trybie alarmowym niewykonalny.

### 4.2. Rozwiązanie

Deklarowane przez M5PM1 stany poboru:

| Stan | Pobór @4.2 V | Szacowane czuwanie z 250 mAh |
|---|---|---|
| Power off | 14 µA | — |
| L1 | 52 µA | ~200 dni (teoretycznie) |
| L2 | 102 µA | ~100 dni |
| L3A | 37 mA | ~7 h |
| Pełne obciążenie | 519 mA | ~30 min |

Przyjęty schemat po zgaszeniu stacyjki:

```
  ZANIK VBUS
      │
      ▼
  0–2 min:  ekran włączony, wyniki widoczne, alarm nieuzbrojony
      │     (użytkownik może wrócić i zobaczyć wyniki jazdy)
      ▼
  2 min:    zapis do NVS → ekran gaśnie → deep sleep
      │
      ▼
  ALARM:    ESP32 w deep sleep, IMU w trybie low-power
            wybudzenie: ruch (V2 tak) lub timer ~2 s (V2 nie)
      │
      ├──► ruch wykryty  ──► wybudzenie, ocena, sygnalizacja
      └──► VBUS wraca    ──► rozbrojenie, nowa sesja jazdy
```

**Realny cel: kilka dni czuwania**, nie 200. Rzeczywisty pobór z aktywnym IMU
i okresowymi wybudzeniami zmierzymy na sprzęcie (V4).

### 4.3. Wpływ sygnalizacji dźwiękowej

Wzmacniacz 1 W przy ciągłej sygnalizacji to prąd rzędu setek mA. Ciągła sygnalizacja
(§20, trzeci stopień) musi mieć **twardy limit czasu** — inaczej rozładuje baterię
i alarm przestanie pilnować motocykla. Propozycja: maks. 30 s ciągłego dźwięku,
potem powrót do czuwania z podwyższoną czułością.

### 4.4. Poprawki do specyfikacji funkcjonalnej

- **§18** — okres oczekiwania skrócony z 3 do **2 minut** (decyzja z 2026-08-28);
  ekran świeci przez ten czas, potem gaśnie.
- **§19** — dopisać: tryb alarmowy działa w deep sleep, ekran wyłączony.
- **§20** — dopisać limit czasu ciągłej sygnalizacji.

---

## 5. Detekcja stanu stacyjki

Cała maszyna stanów urządzenia wisi na jednym sygnale: **czy jest zasilanie zewnętrzne**.

### 5.1. Pułapka

W M5PM1 rejestr `0x12` bit 0 to **status ładowania**, a nie obecność VBUS. Gdy bateria
naładuje się do pełna, ładowanie się kończy **mimo podłączonego zasilania**.

Naiwna implementacja (`ładowanie == stacyjka ON`) po dłuższej jeździe uzna zakończenie
ładowania za zgaszenie stacyjki, odliczy 2 minuty i **uzbroi alarm w trakcie jazdy**.

### 5.2. Podejście

Detekcja zasilania jest odizolowana za interfejsem `PowerSource` z jedną odpowiedzią:
`External / Battery`. Implementacja korzysta z najlepszego dostępnego sygnału,
który potwierdzimy testem V1. Kolejność preferencji:

1. dedykowany bit VBUS w M5PM1, jeśli istnieje;
2. `M5.Power.isCharging()` **OR** napięcie baterii ≥ progu z histerezą;
3. sam próg napięciowy z histerezą i filtrem czasowym.

Niezależnie od wybranego wariantu: **zmiana stanu musi być potwierdzona przez
kilka sekund**, zanim wywoła przejście w maszynie stanów. Chwilowy zanik napięcia
przy rozruchu motocykla nie może zerować sesji jazdy.

---

## 6. Pamięć nieulotna

### 6.1. Problem zużycia flash

Zapis przy każdym nowym rekordzie zajechałby NVS — w intensywnej jeździe rekord może
się poprawiać kilkanaście razy na minutę.

### 6.2. Strategia

- Wyniki żyją w RAM i tam są aktualizowane bez ograniczeń.
- Zapis do NVS: **co ~30 s, tylko jeśli coś się zmieniło**.
- Zapis **natychmiastowy** przy wykryciu zaniku VBUS — i tu mamy luksus, którego
  zwykle nie ma: bateria daje nam całe minuty na spokojny zapis, żadnego wyścigu
  z zanikającym napięciem.
- Zapis przed wejściem w deep sleep.

### 6.3. Zawartość

| Klucz | Zawartość | Kiedy się zmienia |
|---|---|---|
| `max_all` | 5 wartości MAX OGÓLNIE | podczas jazdy, reset ręczny |
| `ride_last` | 5 wartości OSTATNIA JAZDA | podczas jazdy, start sesji |
| `alarm_en` | stan modułu alarmowego | krótkie naciśnięcie przycisku |
| `mount_cal` | kalibracja orientacji montażu | przytrzymanie 10 s |
| `cfg_ver` | wersja schematu danych | migracja przy aktualizacji firmware |

`cfg_ver` pozwala bezpiecznie zmienić format danych w przyszłej wersji firmware
zamiast czytać śmieci. Serializacja jest **jawna, pole po polu** — zrzut struktury
przez `putBytes(&values, sizeof(values))` wiązałby format na flashu z układem pól
w pamięci, więc dodanie szóstej wartości odczytałoby stare dane jako śmieci.

### 6.4. Co odtwarzać przy starcie — rozstrzygnięcie

Specyfikacja mówi dwie rzeczy, które przy dosłownym czytaniu się kłócą:

- **§6.1** — przy starcie rozpoczyna się nowa sesja, OSTATNIA JAZDA zostaje wyzerowana.
- **§25** — po wyłączeniu motocykla wyniki ostatniej jazdy pozostają dostępne.

Sprzeczność jest pozorna i znika, gdy rozróżnimy **dlaczego** urządzenie się uruchomiło:

| Start | Znaczenie | OSTATNIA JAZDA |
|---|---|---|
| jest zasilanie zewnętrzne | stacyjka właśnie włączona | zerowana — nowa sesja (§6.1) |
| tylko bateria | restart po awarii, watchdog | **odtwarzana** z pamięci (§25) |

Bez tego rozróżnienia zawieszenie się urządzenia na parkingu kasowałoby wyniki
przejechanej trasy — mimo że motocykl w ogóle nie był uruchamiany.

MAX OGÓLNIE jest odtwarzane zawsze, w obu przypadkach.

---

## 7. Kalibracja montażu — niepełna obserwowalność

### 7.1. Problem

Kalibracja na postoju (§14) mierzy wektor grawitacji w układzie urządzenia. To określa
**dwa z trzech** stopni swobody orientacji. Trzeci — obrót wokół osi pionowej, czyli
odpowiedź na pytanie „w którą stronę jest przód motocykla" — pozostaje nieokreślony.

Bez niego nie da się rozdzielić przechyłu od pochylenia ani przyspieszenia od
przyspieszenia bocznego.

### 7.2. Rozwiązanie MVP

**Konwencja montażu.** Zakładamy, że wybrana oś urządzenia wskazuje przód motocykla;
oś jest parametrem konfiguracyjnym. Kalibracja mierzy grawitację, a oś „przód"
ortogonalizujemy względem zmierzonego pionu.

### 7.3. Automatyczne wykrywanie orientacji montażu

Pytanie brzmiało: czy urządzenie może samo ustalić, gdzie jest przód, a gdzie tył?
**Tak — i co ciekawe, nie potrzebuje do tego GPS.**

Trzeci stopień swobody rozkłada się na dwa niezależne pytania, które rozwiązuje się
osobno:

#### Krok 1 — która oś jest wzdłużna (bez zwrotu)

Motocykl to pojazd, którego **dominującym ruchem obrotowym jest przechylanie**.
Przez całą jazdę oś wzdłużna zbiera nieporównanie więcej ruchu niż pozostałe dwie.

Wystarczy zbierać macierz kowariancji sygnału żyroskopu:

```
C = Σ (ω − ω̄)(ω − ω̄)ᵀ
```

Wektor własny odpowiadający **największej wartości własnej** to oś wzdłużna motocykla.
Metoda nie wymaga żadnego udziału użytkownika ani żadnego dodatkowego czujnika —
tylko przejechania kawałka drogi z zakrętami.

Wynik jest jednak **osią, nie strzałką**: przód i tył są nierozróżnialne.

#### Krok 2 — w którą stronę tej osi jest przód

Tu jest ładna sztuczka. **Motocykl przechyla się w tę stronę, w którą skręca** —
inaczej niż samochód. Czyli przechył i prędkość odchylania są dodatnio skorelowane:

```
φ > 0 (przechył w prawo)  ⟺  r > 0 (odchylanie w prawo)
```

Jeśli przyjmiemy zwrot osi wzdłużnej odwrotnie, układ się przewraca: przechył zmienia
znak, prędkość odchylania wokół pionu nie — i korelacja staje się **ujemna**.

Wystarczy więc liczyć znak `Σ φ·r` przez kilka zakrętów. Dodatni → zwrot poprawny.
Ujemny → obrócić o 180°. **Bez GPS, bez udziału użytkownika, kilkanaście zakrętów.**

#### Krok 2b — wariant z GPS (szybszy i pewniejszy)

Mając prędkość z GPS zwrot ustala się jeszcze prościej: różniczkujemy prędkość GPS
i korelujemy z poziomą składową akcelerometru. **Hamowanie jest tu najlepszym
sygnałem** — to najsilniejsze i najbardziej powtarzalne zdarzenie wzdłużne na
motocyklu, do −0,8 g. Kilka mocniejszych hamowań rozstrzyga sprawę jednoznacznie.

#### Podsumowanie obserwowalności

| Stopień swobody | Źródło | Potrzebny GPS? | Kiedy się ustala |
|---|---|---|---|
| Pion (2 DOF) | grawitacja na postoju | nie | natychmiast |
| Oś wzdłużna | PCA żyroskopu | nie | kilka minut jazdy |
| Zwrot przód/tył | korelacja φ·r | nie | kilkanaście zakrętów |
| Zwrot przód/tył | korelacja z GPS | tak | kilka hamowań |

#### Ograniczenia, o których trzeba pamiętać

- **Wymaga jazdy.** Przy pierwszym uruchomieniu urządzenie nie wie nic — potrzebny
  jest bootstrap: konwencja montażu albo stan „uczę się" na ekranie.
- **Autostrada nie wystarczy.** Bez zakrętów PCA nie ma czego rozdzielać. Algorytm
  musi mierzyć własną pewność i nie ogłaszać wyniku, dopóki jej nie osiągnie.
- **Montaż musi być sztywny.** Urządzenie przykręcone do ramy — to założenie jest
  spełnione z definicji, ale luźny uchwyt zepsuje wszystko.

#### Decyzja projektowa

MVP zostaje przy **konwencji montażu** — jest deterministyczna, testowalna
i działa od pierwszej sekundy. Auto-detekcja wchodzi jako **proces uczący w tle**,
który po zebraniu wystarczającej pewności proponuje korektę i prosi o potwierdzenie
na ekranie. Użytkownik nigdy nie zostaje z cichą zmianą układu odniesienia.

Najważniejsze: **cały ten algorytm da się opracować offline na nagraniu CSV**
z jazdy testowej. Zero iteracji na motocyklu — to kolejny argument za tym, żeby
rejestrator surowych danych (etap E3) był gotowy przed pierwszym wyjazdem.

---

## 8. Wibracje i parametry akwizycji

Silnik motocykla generuje wibracje o dużej amplitudzie i wysokiej częstotliwości.
Nieodfiltrowane zaszumią IMU i wygenerują fałszywe rekordy.

| Parametr | Wartość wstępna | Uzasadnienie |
|---|---|---|
| Zakres żyroskopu | ±1000 dps | przechylanie motocykla to <300 dps, zapas na wstrząsy |
| Zakres akcelerometru | ±8 g | dziury w drodze potrafią dać >4 g |
| ODR | 100 Hz | dynamika motocykla mieści się poniżej 20 Hz |
| Filtr | dolnoprzepustowy ~10 Hz | odcina wibracje silnika |

Wszystkie wartości są **wstępne** i zostaną zweryfikowane na danych z prawdziwej jazdy.
Montaż na gumowym amortyzatorze mocno pomoże niezależnie od filtrowania.

---

## 9. Obsługa przycisku — rozstrzygnięcie §23

Specyfikacja §23 wymaga, żeby przytrzymanie 10 s nie wywołało po drodze funkcji
przypisanej do 3 s.

**Rozwiązanie:** akcja wykonuje się **po puszczeniu** przycisku, według najdłuższego
osiągniętego progu. W trakcie trzymania ekran pokazuje, co się stanie:

```
   0.0 s ─────► puszczenie → PRZEŁĄCZ ALARM
   2.0 s ─────► puszczenie → RESET WYNIKÓW
   4.0 s ─────► puszczenie → KALIBRACJA
```

Progi skrócone po pierwszych testach na sprzęcie (2026-08-28). Specyfikacja mówiła
o 3 s i 10 s, ale dziesięć sekund z przyciskiem pod palcem okazuje się męczące
i sprawia wrażenie, że urządzenie się zawiesiło. Trzy równe pasma po 2 sekundy
są łatwiejsze do wyczucia — także w rękawicy.

Z paskiem postępu i nazwą nadchodzącej akcji. Użytkownik widzi konsekwencję **zanim**
puści przycisk — może trzymać dalej albo, jeśli pomylił progi, zaczekać do następnego.

Drugi przycisk (KEY2) pozostaje wolny — kandydat na przełączanie widoków ekranu
i regulację jasności.

---

## 10. Architektura oprogramowania

### 10.1. Zasada naczelna: algorytmy oddzielone od sprzętu

Cała logika obliczeniowa jest **czystym C++ bez zależności od M5Unified**:

```
lib/motion/     fuzja orientacji, kalibracja montażu, metryki jazdy
lib/input/      maszyna stanów przycisku
lib/alarm/      logika detekcji ruchu i eskalacji sygnalizacji
```

Powód jest praktyczny: te same pliki kompilują się w środowisku `native` PlatformIO,
więc dane z prawdziwej jazdy przepuszczamy przez algorytm **na komputerze**. Strojenie
filtru to sekundy zamiast cyklu „przekompiluj → wgraj → wyjedź na motocyklu".

Kod zależny od sprzętu (ekran, IMU, PMIC, NVS, głośnik) siedzi w `src/` i komunikuje
się z algorytmami przez proste struktury danych.

### 10.2. Struktura

```
platformio.ini          dwa środowiska: sticks3 (firmware) + native (analiza)
boards/                 definicja płytki M5StickS3
lib/
  motion/               ★ czyste C++ — fuzja, kalibracja, metryki
  input/                ★ czyste C++ — FSM przycisku
  alarm/                ★ czyste C++ — detekcja ruchu
src/
  main.cpp              pętla główna
  config.h              progi i stałe w jednym miejscu
  hal/                  PowerSource, Imu, Store, Buzzer
  ui/                   ekrany
  log/                  rejestrator surowych danych
tools/replay/           odtwarzanie CSV przez algorytm na komputerze
test/                   testy jednostkowe algorytmów (native)
```

### 10.3. Rejestrator surowych danych

Firmware równolegle z normalną pracą zapisuje surowe próbki IMU do pliku na Flash
i/lub strumieniuje po USB w formacie CSV. To jest materiał do strojenia algorytmu
po pierwszej prawdziwej jeździe.

Format: `t_ms,ax,ay,az,gx,gy,gz,vbus,state`

Włączane flagą kompilacji — w wersji produkcyjnej wyłączone.

---

## 11. Zasilanie z motocykla

Poza zakresem firmware, ale wpływa na niezawodność.

Instalacja motocyklowa nie jest stabilnym źródłem 12 V:

- **load dump** — odłączenie akumulatora przy pracującym alternatorze potrafi dać
  skoki do kilkudziesięciu V,
- rozruch chwilowo zapada napięcie,
- w instalacji jest szum od zapłonu.

Wymagania dla przetwornicy:

- wejście **9–36 V** (nie 12 V nominalne),
- bezpiecznik na linii zasilania,
- dioda TVS na wejściu,
- podłączenie do obwodu **przełączanego stacyjką**, nie do stałego plusa.

Zapad napięcia przy rozruchu jest jednym z powodów, dla których zmiana stanu
zasilania musi być filtrowana czasowo (§5.2).

---

## 12. Plan etapów

| Etap | Zakres | Weryfikacja | Stan |
|---|---|---|---|
| E0 | Szkielet projektu, konfiguracja, definicja płytki | kompilacja obu środowisk | ✅ gotowe |
| E1 | Algorytmy: orientacja, kalibracja, metryki (czyste C++) | testy jednostkowe native | ✅ gotowe |
| E2 | Firmware: boot, ekran, IMU, podgląd na żywo | wgranie na sprzęt | ✅ gotowe |
| E5 | NVS, dwa zestawy wyników, kalibracja, alarm | test cyklu zasilania | ✅ gotowe |
| E7 | Przycisk: trzy progi, reset, kalibracja | test ręczny | ✅ gotowe |
| E3 | Rejestrator CSV + narzędzie replay | ★ **jazda testowa** | — |
| E6 | Maszyna stanów zasilania — wykrywanie stacyjki | test stacyjki | — |
| E8 | Alarm: deep sleep, wake-on-motion, sygnalizacja | test nocnego czuwania | — |
| GPS | Parser NMEA, prędkość do filtru i do rekordów | jazda z modułem | — |
| E4 | Strojenie algorytmu na prawdziwych danych | replay z nagrania | czeka na dane |
| E9 | Hardening: watchdog, testy terenowe | jazda długodystansowa | — |

Wszystkie etapy poza E4 można zrobić bez danych z jazdy. E4 czeka na nagranie
z pierwszego prawdziwego wyjazdu.

---

## 13. Procedura wgrywania firmware

Spisane po tym, jak pierwsze wgranie kosztowało godzinę szukania nieistniejącego
błędu (2026-08-28).

### 13.1. Wejście w tryb pobierania

M5StickS3 z fabrycznym UiFlow **nie oddaje portu na programowy reset** przez DTR/RTS.
`pio run -t upload` kończy się wtedy `Failed to connect: No serial data received`.

Ręcznie: **przytrzymać boczny przycisk reset przez ~2 s**, aż mrugnie zielona dioda.

### 13.2. Pułapka — wyjście z trybu pobierania ⚠️

**`esptool` swoim „Hard resetting via RTS pin" NIE wyprowadza urządzenia z trybu
pobierania z powrotem do aplikacji.** Trzeba **kliknąć reset fizycznie** (krótkie
naciśnięcie) albo przepiąć USB.

Dopóki się tego nie zrobi, każde kolejne wgranie wygląda na nieudane:

- czarny ekran,
- martwy port szeregowy,
- port zgłasza się jako `USB JTAG/serial debug unit` (VID:PID `303A:1001`).

To jest mylące, bo **wygląda identycznie jak zepsuta konfiguracja płytki**.
W praktyce doprowadziło do czterech kolejnych „napraw" PSRAM i trybu USB, z których
żadna niczego nie zmieniała — firmware przez cały czas był zapisany poprawnie
i zweryfikowany hashem.

**Zasada: zanim zaczniesz podejrzewać konfigurację, kliknij reset.**

### 13.3. Identyfikacja stanu po VID:PID

| PID | Stan |
|---|---|
| `303A:8120` | fabryczne UiFlow 2.0 (TinyUSB CDC) |
| `303A:0009` | ROM download mode przez USB-OTG |
| `303A:1001` | USB-Serial-JTAG — **sam się zgłasza sprzętowo**, nie dowodzi, że aplikacja żyje |

Ostatni wiersz jest istotny: obecność portu nie jest dowodem, że firmware wystartował.

### 13.4. Podgląd portu szeregowego

Otwarcie portu domyślnie szarpie DTR, co resetuje urządzenie i unieważnia uchwyt.
Do nasłuchu otwierać port z `dtr = False` i `rts = False` **ustawionymi przed**
`open()`, oraz przewidzieć ponowne wpięcie po re-enumeracji.

---

## 14. Konfiguracja integracji przez USB

Sieć domową i token konta trzeba jakoś wprowadzić do urządzenia. Docelowo robi
to formularz na telefonie (K4), ale zanim on powstał, potrzebna była droga
najkrótsza: użytkownik i tak siedzi przy komputerze, gdy kopiuje token ze
strony, a monitor portu szeregowego jest otwarty przy każdym wgrywaniu.

### 14.1. Komendy

Wpisywane w monitorze portu (`pio device monitor`), każda zakończona enterem:

| Komenda | Działanie |
|---|---|
| `SIEC=<nazwa>` | nazwa sieci WiFi (alias: `SSID=`) |
| `HASLO=<hasło>` | hasło sieci; puste = sieć otwarta |
| `TOKEN=<token>` | token konta przepisany ze strony |
| `STAN` | pokazuje bieżące ustawienia |
| `KASUJ` | czyści całą konfigurację |

Format `KLUCZ=wartość`, a nie jednoliterowe skróty jak w rejestratorze (`L`,
`D<nr>`, `X`): hasło WiFi może zawierać spacje, więc potrzebny jest jednoznaczny
separator. Wielkość liter w kluczu nie ma znaczenia.

### 14.2. Rozstrzygnięcia

**Wartość idzie dosłownie**, razem z odstępami — `HASLO= tajne` ustawia hasło
zaczynające się spacją. Obcinanie białych znaków dawałoby konfigurację, która
wygląda dobrze, a nigdy się nie połączy.

**Wartość odrzucona nie zmienia niczego.** Za długa, ze znakiem sterującym,
z odstępem w tokenie — konfiguracja zostaje w poprzednim stanie, a na port idzie
komunikat. Połowa hasła jest gorsza niż jego brak, bo wygląda jak ustawienie.

**Linia dłuższa niż bufor (160 B) jest odrzucana w całości.** Obcięty token
zapisałby się jako kompletny i dawał 401 bez żadnej wskazówki dlaczego.

**Hasło i token nigdy nie są wypisywane jawnie** — `STAN` pokazuje hasło jako
fakt („ustawione"), a token zamaskowany (`****wxyz`). Wydruk z portu bywa
wklejany do zgłoszeń.

**Jeden czytnik portu.** Linie czyta `pumpSerial()` w `main.cpp` i rozdziela:
najpierw konfiguracja, potem — jeśli to nie jej komenda — rejestrator surowych
danych (`RawLogger::handleCommand`). Dwa niezależne czytniki tego samego portu
podkradałyby sobie znaki.

---

## 15. Konfiguracja z telefonu i wysyłka wyników

Docelowa droga konfiguracji: urządzenie stawia własną sieć WiFi, właściciel
wypełnia formularz na telefonie, urządzenie od razu sprawdza, czy token działa.
Konfiguracja przez USB (§14) zostaje jako droga serwisowa — przydaje się, gdy
punkt dostępowy nie wstaje.

### 15.1. Przebieg

1. **KEY2 przez 6 s** — ekran INTEGRACJA stawia punkt dostępowy i pokazuje trzy
   rzeczy do przepisania: nazwę sieci `MOTOBOX-<4 znaki device_id>`, hasło
   i adres `192.168.4.1`.
2. Telefon łączy się z siecią; captive portal zwykle otwiera formularz sam.
   Adres jest na ekranie, bo „zwykle" nie znaczy „zawsze" — Android z prywatnym
   DNS potrafi przekierowanie zignorować.
3. Formularz: sieć (lista ze skanu), hasło, token. Puste pole hasła lub tokena
   znaczy **bez zmian** — poprawienie samej sieci nie kasuje tokena.
4. Po zapisie punkt dostępowy gaśnie, urządzenie łączy się z siecią domową,
   sprawdza token (`GET /api/v1/ping`) i **od razu wysyła zaległe przejazdy**.
5. Wynik pokazuje ekran urządzenia: `INTEGRACJA OK` z liczbą wysłanych
   przejazdów, `TOKEN ODRZUCONY`, `BRAK SIECI` albo `SERWER MILCZY`.

Wynik trafia na ekran, a nie do przeglądarki, bo sprawdzenie tokena wymaga
połączenia z siecią domową — a wtedy telefon traci łączność z urządzeniem.
Strona po zapisie mówi o tym wprost, zamiast obiecywać wynik, którego nikt
już nie zobaczy.

### 15.2. Kiedy urządzenie wysyła samo

**Tylko w oknie po zgaszeniu stacyjki** (stan Cooldown, 2 minuty). Powody:

- przejazd jest właśnie skończony, więc nie ma czego mierzyć — a wysyłka
  blokuje pętlę główną na kilkanaście sekund,
- motocykl stoi w garażu, czyli w zasięgu sieci domowej,
- urządzenie jeszcze nie śpi.

**W czuwaniu urządzenie nie budzi się, żeby wysyłać.** Radio to ~100 mA przy
baterii 250 mAh; dwa dni czuwania są warte więcej niż wynik dostarczony o poranek
wcześniej. Zaległości poczekają do następnego przejazdu.

### 15.3. Reakcja na błędy

| Sytuacja | Reakcja |
|---|---|
| brak sieci, timeout, 5xx, 429, 422 | odstęp rośnie dwukrotnie: 30 s, 1 min, 2 min… do 15 minut |
| 401/403 (token zły) | **wysyłka staje całkowicie** do zmiany konfiguracji |
| 200 bez `accepted_through` | znacznik zostaje — przejazdy wrócą przy następnej próbie |

Zatrzymanie po 401 jest istotne: bez niego źle przepisany token oznaczałby
budzenie radia w kółko aż do rozładowania baterii, i to bez cienia szansy
na powodzenie.

Znacznik wysyłki przesuwa się **wyłącznie** na podstawie liczby od serwera.
Odpowiedź 200 bez `accepted_through` zostawia kolejkę nietkniętą — powtórna
wysyłka jest zawsze lepsza niż ciche skasowanie przejazdu.

### 15.4. Certyfikat

Wbudowany **ISRG Root X1** (Let's Encrypt), ważny do 2035. Nie przypinamy
odcisku serwera: certyfikat Let's Encrypt wymienia się co ~60 dni, a przypięcie
unieruchamiałoby urządzenie po każdym odnowieniu — bez możliwości naprawy
inaczej niż kablem, w każdym urządzeniu z osobna.

**Zmiana wystawcy certyfikatu na serwerze zerwie połączenie wszystkim
urządzeniom w terenie.** Taka zmiana wymaga wcześniejszego wydania firmware
z nowym certyfikatem.

### 15.5. Hasło punktu dostępowego

Wyprowadzone z `device_id`, więc **powtarzalne** — telefon łączy się
z zapamiętanej sieci także po miesiącach. Dziesięć znaków z alfabetu bez par
mylących się wzrokiem (bez `O` i `0`, bez `I` i `1`), bo przepisuje się je
z ekranu 240×135.

Świadomy kompromis: `device_id` jest publiczny, a firmware otwarty, więc hasło
da się policzyć. Chroni nas okno czasowe, nie sekret — sieć żyje tylko wtedy,
gdy właściciel stoi przy motocyklu z otwartym ekranem INTEGRACJA. Sieci bez
hasła celowo nie stawiamy: tam każdy przechodzień podmieniłby token jednym
kliknięciem, bez liczenia czegokolwiek.

---

## 16. Bramka prędkości — ustalenie na etap GPS

Decyzja użytkownika z 2026-09-01, do wykonania razem z modułem GPS. Zapisana
teraz, bo powód jest konkretny i łatwo go zgubić: **przechył motocykla przy
2 km/h nie jest rekordem przechyłu.**

### 16.1. Problem

Dziś rekordy zbierane są zawsze, gdy jest kalibracja i włączony zapłon:

```cpp
if (g_mount.isCalibrated() && g_deviceState.state() == state::DeviceState::Riding) {
    g_metrics.update(g_orientation.state());
    g_rideClock.update(!g_orientation.state().stationary, millis());
}
```

Ten warunek nie odróżnia jazdy od manewrowania. Dojazd do skrzyżowania,
utrata równowagi przy 3 km/h i motocykl prawie na ziemi — to zapisze się jako
rekord przechyłu całej sesji. Ta sama rodzina przypadków: postawienie na
bocznej nóżce przy włączonym zapłonie, prowadzenie motocykla obok siebie,
manewrowanie w garażu, szarpnięcie sprzęgłem przy ruszaniu.

Skutek jest gorszy niż pojedyncza zła liczba: **rekord przypadkowy zawsze
wygrywa z prawdziwym**, bo jest większy. Kolumna „maksymalny przechył" przestaje
znaczyć cokolwiek.

### 16.2. Rozwiązanie

Rejestrowanie pomiarów tylko powyżej progu prędkości. Przejazd również zaczyna
się od przekroczenia progu, nie od włączenia zapłonu.

| Parametr | Wartość wyjściowa | Po co |
|---|---|---|
| próg włączenia | 5 km/h | poniżej tego motocykl jest manewrowany, nie prowadzony |
| próg wyłączenia | 3 km/h | histereza — bez niej rejestracja migocze przy 4,9/5,1 km/h |
| wybieg hamowania | 2 s | patrz niżej |
| czas podtrzymania bez fixu | ~15 s | tunel, wiadukt, gęsta zabudowa |

Progi trafiają do `config.h` — pierwsza prawdziwa jazda i tak je zweryfikuje.
Sama bramka to czyste C++ (`motion::SpeedGate`), więc daje się przetestować
na komputerze jak reszta algorytmów.

### 16.3. Trzy pułapki, o których trzeba pamiętać przy pisaniu

**Hamowanie do zera.** Awaryjne hamowanie kończy się na 0 km/h, a jego ostatnia
faza bywa najostrzejsza — czyli bramka odcięłaby dokładnie to, co najciekawsze.
Stąd wybieg: przez 2 s po spadku poniżej progu nadal rejestrujemy, skoro chwilę
wcześniej jechaliśmy.

**Utrata fixu nie może oznaczać „nie rejestruję nic".** Po upływie czasu
podtrzymania wracamy do dzisiejszej reguły opartej na `stationary` z IMU.
Awaria modułu GPS ma degradować urządzenie do stanu sprzed GPS, a nie
wyłączać pomiary.

**Prowadzenie motocykla pieszo to 4–5 km/h**, czyli okolice progu. Ratuje nas
to, że prowadząc trzyma się kierownicę i motocykl stoi pionowo — ale gdyby
okazało się to problemem, próg idzie w górę, nie histereza w dół.

### 16.4. Jak to sprawdzić bez zgadywania

Przy pierwszej prawdziwej jeździe włączyć rejestrator surowych danych
(`MMB_RAW_LOGGER=1`, §10.3). Mając nagranie z GPS, da się policzyć offline,
ile rekordów bramka faktycznie odcięłaby i czy 5 km/h to właściwy próg —
zamiast dobierać go na wyczucie po fakcie. Ten sam materiał służy do strojenia
filtrów orientacji.
