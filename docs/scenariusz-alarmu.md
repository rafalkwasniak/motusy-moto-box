# Scenariusz działania alarmu — do zatwierdzenia

Wersja robocza po pierwszych testach E8 na sprzęcie (2026-08-28).
Zastępuje dotychczasowe, niespójne zachowanie jednym kompletem reguł.

---

## Reguła naczelna

**Ekran zgaszony na baterii + moduł alarmu włączony ⇒ silnik alarmu uzbrojony.**

Każda ścieżka, która gasi ekran, przechodzi przez tę jedną regułę: koniec odliczania,
koniec sygnalizacji, wyciszenie przyciskiem, timeout po obudzeniu ekranu. Dzięki temu
nie ma żadnej drogi, na której napis „ALARM" świeci w rogu, a silnik realnie nie czuwa —
czyli błędu znalezionego w testach.

Uzbrojenie zawsze bierze **aktualną pozycję** jako odniesienie (motocykl na bocznej
stopce nie wywoła własnego alarmu).

---

## 1. Odliczanie po odłączeniu zasilania

| Co | Było | Będzie |
|---|---|---|
| Okno „tętna" ładowania | 25 s | **10 s** (do potwierdzenia pomiarem „luka" z ekranu SPRZĘT) |
| Potwierdzenie zaniku | 5 s | **2 s** (okno tętna już filtruje zapady rozruchu) |
| Start odliczania | od momentu wykrycia | **antydatowany do momentu faktycznego odłączenia** |

Efekt: zegarek pojawia się ~12 s po wyjęciu kabla i pokazuje już np. `1:48` —
a ekran gaśnie **dokładnie 2:00 od odłączenia**, nie 2:30.

## 2. Sekwencja alarmowa

Odniesienie pozycji zostaje z momentu uzbrojenia. Jeśli motocykl został przestawiony
i stoi inaczej, warunek naruszenia trwa — eskalacja idzie sama do końca. Celowo:
złodziej odchodzący z motocyklem ma słyszeć syrenę.

| Stopień | Wyzwolenie | Dźwięk | Ekran |
|---|---|---|---|
| 1 | 1. naruszenie | **5 piknięć** (dłużej niż teraz) | budzi się: `RUCH!` |
| 2 | 2. naruszenie | 3 długie tony | `RUCH!` |
| 3 | 3. naruszenie | **syrena modulowana** (płynny przestrój 1000↔3000 Hz, jak prawdziwy alarm) — **BEZ limitu czasu, wyje aż do wyciszenia, stacyjki albo rozładowania** | `RUCH!` |

- Zdjęty: limit 30 s syreny. Zdjęty: tryb oszczędzania przy słabej baterii.
  Ma ostrzegać — jak się rozładuje, to się rozładuje.
- Po zakończeniu piknięć stopnia 1/2 bez dalszego ruchu: ekran gaśnie,
  czuwanie trwa.
- **Reset eskalacji po 2 minutach ciszy — do zera, nie o stopień.** Bez tego
  przypadkowe trącenia sumowałyby się przez cały postój: ktoś trąca motocykl
  na zlocie i odchodzi, pół godziny później druga osoba ociera się o niego
  i dostaje syrenę, choć to jej pierwszy kontakt. Każdy „nowy gość" ma dostać
  łagodne ostrzeżenie nr 1. Trwająca syrena nie jest przerywana.
- Po **każdej** sygnalizacji tor audio jest wyłączany → koniec trzeszczenia
  z głośnika w czuwaniu.

## 3. Przyciski — podział ról

Zamienione 2026-08-28: najczęstsza czynność (oglądanie wyników) trafiła na
wygodniejszy w dosięgu KEY1, a rzadkie akcje — na KEY2.

| Przycisk | Akcja |
|---|---|
| **KEY1** klik | wyniki → strony archiwum → wyniki |
| **KEY1** hold | widoki serwisowe (diagnostyka / sprzęt) |
| **KEY2** klik | przełączenie modułu alarmu |
| **KEY2** 2 s | reset wyników |
| **KEY2** 4 s | kalibracja montażu |

Gdy alarm wyje, wycisza go **dowolny** przycisk — w takiej chwili nikt nie
zastanawia się, który jest który.

## 3a. Przycisk akcji — pełny scenariusz

| Sytuacja | Krótki klik KEY2 robi |
|---|---|
| Alarm **wyje/pika** | **tylko wycisza.** Moduł zostaje WŁĄCZONY. Ekran pokazuje `WYCISZONO — CZUWA DALEJ`, po 15 s gaśnie i uzbraja się od nowa w aktualnej pozycji |
| Okno 15 s po wyciszeniu | przełącza moduł (można wyłączyć alarm na dobre: klik nr 2) |
| Czuwanie, ekran zgaszony | **budzi ekran** (podgląd wyników), nic nie przełącza |
| Ekran obudzony na baterii | przełącza moduł WŁ/WYŁ (jak dotychczas) |
| Jazda / odliczanie | przełącza moduł WŁ/WYŁ (jak dotychczas) |

Logika dwustopniowa przy wyjącym alarmie (klik 1 = cisza, klik 2 = wyłączenie modułu)
chroni przed najczęstszym błędem: wyciszyłem fałszywy alarm i nieświadomie zostawiłem
motocykl bez ochrony.

## 4. Ekran na baterii — timeout

Każde obudzenie ekranu na baterii (przyciskiem lub alarmem) uruchamia **timeout 30 s**
(po wyciszeniu: 15 s). Po nim ekran gaśnie i — jeśli moduł włączony — silnik uzbraja
się ponownie. To naprawia „ekran został włączony na zawsze".

Przełączenie modułu na WŁĄCZONY przy zgaszonym silniku również kończy się tak samo:
komunikat → timeout → zgaśnięcie → uzbrojenie. To naprawia „włączyłem alarm ponownie
i już się nie wzbudza".

## 5. Stacyjka (bez zmian)

Powrót zasilania w **dowolnym** momencie: natychmiastowe wyciszenie, rozbrojenie,
nowa sesja LOTKA.

---

## Pytania otwarte przed wdrożeniem

1. Ile pokazuje `luka` na ekranie SPRZĘT po kilku minutach ładowania pełnej baterii?
   (od tego zależy bezpieczne okno 10 s)
2. Akceptacja zachowania przycisku z §3 (dwustopniowe wyciszenie).
