# Motusy Moto Box — ślad trasy (GPX)

**Status: szkic na kolejny etap. Nic z tego nie jest zaimplementowane i nic
w firmware ani w kontrakcie API się przez ten dokument nie zmienia.**
Notatka z rozmowy projektowej (2026-09-01), spisana po to, żeby ustalenia nie
wyparowały. Do realizacji dopiero po module GPS i po tym, jak `speed_kmh`
i `recorded_at` zaczną się faktycznie wypełniać.

Wersja formatu opisanego niżej: **1** (propozycja).

---

## 1. Po co

Mając GPS, największą wartością nad „maksymalna prędkość" jest cała trasa:
przebieg przejazdu na mapie, dystans, profil prędkości. Problem jest jeden —
trasa potrafi trwać 5–6 godzin, a urządzenie ma zapisywać na flashu i wysyłać
przez łącze, które bywa dostępne raz na dobę.

Zasady, z których wynika reszta dokumentu:

- **Ślad jest opcją, domyślnie wyłączoną.** Trasa to dane innej wagi niż kąt
  przechyłu; ma być świadoma zgoda. Wyłączona opcja znaczy, że nic się nie
  zapisuje — zero zużycia flasha, nie tylko brak wysyłki.
- **Ślady są prywatne i kompletne.** Widoczne wyłącznie dla właściciela konta.
- **Ślad nie zastępuje wyniku przejazdu.** Wynik idzie tak jak dziś; ślad to
  osobny, opcjonalny dodatek, którego brak niczego nie psuje.

---

## 2. Format

Punkt startu bezwzględny, potem przesunięcia względem poprzedniego punktu:

```
start:   1957648,5133528
punkty:  113,-182,5;26,-3,1;340,84,12
         └ dlon,dlat,dt[s]
```

**Skala 1e-5 stopnia**, czyli wartość całkowita = stopnie × 100 000. Kolejność
`lon,lat` — jedna kolejność, zapisana tutaj, bo pomyłka w tym miejscu daje ślad
gdzieś w Somalii i nikt tego nie zauważy od razu.

### Dlaczego pięć miejsc, a nie sześć

| | rozdzielczość szer. | rozdzielczość dł. (52°N) |
|---|---|---|
| 1e-5° | 1,11 m | 0,69 m |
| 1e-6° | 0,11 m | 0,07 m |

GPS ma błąd 2–5 m. Szósta cyfra opisuje wyłącznie szum, a kosztuje jedną cyfrę
w **każdej** delcie — około 20% całego śladu.

### Dlaczego delty liczone na liczbach całkowitych

Każdy fix najpierw na `int32` w jednostkach 1e-5, dopiero potem
`delta = teraz − poprzednio`. Arytmetyka jest wtedy dokładna i **błąd się nie
kumuluje**: po sześciu godzinach ostatni punkt jest tak samo dobry jak pierwszy.
Delty liczone z wartości zmiennoprzecinkowych powodowałyby powolny odpływ śladu.

Kluczowa zasada: **delta zawsze względem ostatniego ZAPISANEGO punktu**, nie
względem ostatniego odczytanego. Inaczej pominięte punkty gubią się w sumie.

### Dlaczego czas jest w każdym punkcie

Bez `dt` to nie jest ślad, tylko kształt — strona nie policzy ani prędkości, ani
tempa. Przy zmiennym odstępie jest niezbędny wprost. Ale nawet przy stałym
odstępie i tak jest potrzebny, bo stały odstęp zostaje stały tylko wtedy, gdy nic
się nie psuje, a psuje się rutynowo: tunel, wiadukt, garaż, las, postój na
stacji. Mechanizm oznaczania przerwy trzeba mieć tak czy inaczej, a `dt` jest
najprostszym.

### Dlaczego zapis dziesiętny, a nie polyline

Kodowanie Google'a (base64 + varint) dałoby ślad mniej więcej dwa razy mniejszy.
Świadomie z tego rezygnujemy: zapis dziesiętny czyta się okiem, pisze przez
`itoa`, a parsuje przez `explode`. Przy projekcie, w którym diagnostyka odbywa
się po kablu USB, czytelność formatu jest warta więcej niż 20 kB na przejazd.
Numer wersji formatu zostawia furtkę, gdyby to się kiedyś zmieniło.

---

## 3. Dobór punktów

Cel: ślad ma być **jak najdokładniejszy tam, gdzie coś się dzieje, i jak
najtańszy tam, gdzie się nie dzieje nic**.

### Odrzucone: progi na zakręt i prędkość

Pierwszy pomysł brzmiał „prosta → co 10 s, miasto → co 5 s, zakręty → co 1 s".
Odrzucony, bo takie progi stroi się w nieskończoność i tak nie trafiają
w przypadki brzegowe — łagodny łuk brany 90 km/h wygląda jak prosta, a ścina się
widocznie.

### Docelowe: korytarz (opening window / Reumann–Witkam)

Próbkować zawsze 1 Hz (moduł i tak tyle oddaje, to nic nie kosztuje), a decydować
o **zachowaniu** punktu jednym kryterium: *o ile metrów narysowana linia rozjedzie
się z prawdziwym torem jazdy*.

1. Trzymamy punkt-kotwicę A i bufor fixów od niej.
2. Dla nowego fixu P liczymy największe odchylenie punktów z bufora od odcinka A→P.
3. Przekroczyło ε → poprzedni fix zostaje zapisany jako nowa kotwica, bufor czyścimy.
4. Twarde ograniczniki: nie rzadziej niż co 60 s, nie częściej niż raz na sekundę.

Efekt jest ten sam, o który chodziło w progach, ale wyprowadzony z geometrii —
autostrada dostaje punkt co minutę, ciasny zakręt co sekundę, i skaluje się
z prędkością sam z siebie. Do tego daje gwarancję, której progi nie dadzą:
*ślad nigdzie nie odbiega od trasy więcej niż ε*.

**ε ≈ 8 m, nie 5 m.** Przy 5 m sam szum GPS na prostej zaczyna wyzwalać zapisy.

Odległości liczyć w metrach na płaskiej siatce lokalnej; `cos(lat)` policzone raz
na przejazd wystarczy (na 200 km trasy zmienia się o ~2%, przy korytarzu 8 m bez
znaczenia). Koszt: bufor ≤60 punktów (~700 B) i kilkadziesiąt mnożeń na sekundę.

### Dlaczego to nie jest kwestia rozmiaru

Rachunek dla 6 h jazdy:

| | punktów | bajtów/punkt | razem |
|---|---|---|---|
| surowo 1 Hz | 21 600 | ~11 | ~230 kB |
| stały odstęp 5 s, bez `dt` | 4 320 | ~8,5 | **~37 kB** |
| korytarz ε=8 m, z `dt` | ~3 600 | ~11 | **~40 kB** |

Stały odstęp i korytarz wychodzą **na remis w bajtach** — adaptacja robi mniej
punktów, ale każdy jest droższy (dłuższe odcinki to więcej cyfr w delcie, plus
`dt`). Różnica jest w tym, **gdzie te bajty są wydane**.

Ciasny nawrót, promień 30 m, brany 40 km/h:

| | odstęp punktów | ścięcie zakrętu |
|---|---|---|
| co 5 s | 55 m (~106° łuku) | **~12 m**, nawrót renderuje się jako trójkąt |
| co 1 s | 11 m (~21° łuku) | **0,5 m** |

W drugą stronę: autostrada 120 km/h, stały odstęp 5 s pisze 720 punktów na
godzinę na drodze, która jest prosta. Korytarz napisze 60.

### Droga dojścia

Format z `dt` obsługuje wszystkie trzy warianty **bez żadnej zmiany** — stały
odstęp to po prostu strumień, w którym `dt` zawsze wynosi 5. Kontrakt spisujemy
raz, decymator wymieniamy później, nie ruszając API ani parsera po stronie
Laravela. Dzięki temu można iść po kolei zamiast wybierać:

1. **Stały odstęp** — ~20 linii. Dobre na pierwszą jazdę testową, gdzie pytanie
   brzmi „czy GPX w ogóle wychodzi", a nie „czy jest ładny".
2. **Stały dystans** (punkt co ~25 m) — kilka linii więcej, już wyraźnie lepiej:
   wolne zakręty dostają gęściej niż szybkie proste.
3. **Korytarz** — ~50 linii czystego C++ w `lib/`, z testem natywnym na
   syntetycznej trasie (liczba punktów + maksymalne odchylenie).

Krok 3 da się napisać i przetestować **bez GPS-a**. Mając nagrania z
`RawLogger`, da się nawet porównać warianty na prawdziwej jeździe, zanim
cokolwiek poleci na serwer.

---

## 4. Zapis na urządzeniu

**Ślad idzie na flash w trakcie jazdy, nie do RAM.** Zanik zasilania w piątej
godzinie nie może kosztować całej trasy. Partycja `storage` (4,75 MB,
`motobox_8MB.csv`) już istnieje, a `src/log/RawLogger.h` ma gotowy wzorzec:
bufor w RAM, flush na flash co 2 s, kasowanie najstarszych plików przy braku
miejsca.

Rozmiar: 40 kB na przejazd × 10 przejazdów historii = ~400 kB. Mieści się bez
kompromisów, więc **ślad trzymamy dla tylu przejazdów, ile trzyma historia** —
ale to decyzja świadoma, a nie coś do odkrycia przy pełnej partycji.

---

## 5. Wysyłka

### Osobny endpoint

Ślad nie ma czego szukać w `POST /api/v1/rides`. Tamta przesyłka jest budowana
w statycznym buforze 4 kB (`kMaxPayloadBytes` w `lib/telemetry/TelemetryJson.h`)
i przekazywana jako `const char*` — cała musi istnieć w RAM naraz. To ograniczenie
bufora, nie sieci ani protokołu: dziesięć przejazdów to realnie ~1,5 kB, reszta
jest zapasem, a mały bufor trzyma przesyłkę z dala od sterty potrzebnej pod
handshake TLS (30–45 kB przy mbedtls).

Ślad tego ograniczenia nie dziedziczy, bo leży już na flashu, a `HTTPClient`
umie POST-ować ze `Stream` — 40 kB przechodzi prosto z pliku małym buforkiem,
jednym żądaniem. **Dzielenie na kawałki nie jest potrzebne do zmieszczenia się**;
dawałoby wznawialność, a przy transmisji trwającej kilkanaście sekund to
prawdopodobnie przerost formy. Padnie — powtarza się całość.

### Idempotencja

Ślad wisi na `(device_id, seq)` — tym samym kluczu, co przejazd. Powtórna
wysyłka ma trafić w istniejący wpis, nie zrobić drugiego. To ta sama zasada,
na której stoi całe `accepted_through` w `docs/api-telemetria.md` §3.

Do rozstrzygnięcia przy pisaniu kontraktu: co znaczy „przejazd doszedł, ślad
nie" i odwrotnie. Ślad jest dodatkiem, więc jego brak nie może blokować
przesuwania `accepted_through` — ale urządzenie musi wiedzieć, że ma go jeszcze
dowieźć.

---

## 6. Zachowanie urządzenia

**Bramkowanie fixu.** Punkt startu to pierwszy fix 3D z sensownym HDOP, a nie
pierwszy jaki przyjdzie. Zimny start trwa 30–60 s; bez bramki każdy ślad zaczyna
się skokiem o 200 m.

**Przerwa to segment, nie tylko duże `dt`.** Utrata fixu (tunel, wiadukt, garaż)
i postój — bo stanie w miejscu nie ma po co zapisywać 720 identycznych punktów na
godzinę. Samo `dt=900` powie stronie „minęło 15 minut", ale mapa i tak narysuje
prostą przez pół miasta. GPX ma na to `<trkseg>`; format potrzebuje znacznika
„tu ślad się urwał i zaczyna od nowa".

**GPS gaszony razem ze stacyjką.** W trakcie jazdy urządzenie wisi na zasilaniu
z motocykla (`src/hal/PowerSource.h` mierzy VBUS), więc pobór modułu podczas
zapisu nie ma znaczenia. Zostawiony włączony na czuwaniu zjadłby baterię tak samo
jak WiFi — ~32 mA to kilka godzin zamiast dwóch dni.

---

## 7. Czego tu celowo NIE ma

**Obcinania początku i końca śladu.** Standardowa rada przy śladach GPS brzmi
„utnij pierwsze 200 m, bo tam jest dom właściciela". U nas ślady są prywatne
i widoczne wyłącznie dla właściciela konta, więc to rozwiązywałoby problem,
którego nie ma — a odbierałoby dane, po które użytkownik przyszedł. Ślad ma być
kompletny: od miejsca, w którym motocykl ruszył, do miejsca, w którym stanął.

**Prędkości w każdym punkcie.** Kusi, bo prędkość z dopplera jest dokładniejsza
niż liczona z pozycji, ale kosztuje ~30% rozmiaru. Format jest pozycyjny, więc
czwarte pole można dołożyć później bez przebudowy.

---

## 8. Otwarte, do rozstrzygnięcia przy pisaniu kontraktu

- Kolejność pól i dokładny zapis znacznika przerwy między segmentami.
- Co robi urządzenie, gdy opcja śladu zostanie przełączona w trakcie przejazdu.
- Zachowanie, gdy przejazd wypadł z historii, a jego ślad jeszcze leży na flashu.
- Czy do śladu dokładamy maksymalny przechył na odcinku (czwarte pole). Korytarz
  zagęszcza punkty dokładnie na zakrętach, czyli tam, gdzie IMU ma co powiedzieć —
  dałoby to mapę trasy pokolorowaną kątem przechyłu, czyli ficzer wyraźnie
  ciekawszy niż sama prędkość maksymalna. Koszt: ~3 znaki na punkt.
