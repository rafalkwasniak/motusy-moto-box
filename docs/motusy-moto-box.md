# Motusy Moto Box — Specyfikacja funkcjonalna MVP

## 1. Cel urządzenia

Motusy Moto Box jest autonomicznym urządzeniem montowanym na motocyklu.

Podstawowym zadaniem urządzenia jest rejestrowanie parametrów dynamiki jazdy na podstawie danych z wbudowanego czujnika IMU.

Urządzenie posiada również automatyczny moduł wykrywania poruszenia motocykla po jego wyłączeniu.

Urządzenie nie wymaga telefonu, aplikacji ani zewnętrznego komputera do podstawowego działania.

---

## 2. Platforma sprzętowa

MVP wykorzystuje M5Stack M5StickS3 wyposażony w:

- ESP32-S3
- IMU BMI270
- wyświetlacz
- przycisk użytkownika
- głośnik
- akumulator
- pamięć Flash
- USB-C

Zasilanie podczas pracy pochodzi z instalacji elektrycznej motocykla poprzez przetwornicę 12 V -> 5 V USB-C.

Akumulator M5StickS3 stanowi źródło zasilania podczas zaniku zasilania z motocykla.

---

## 3. Zasilanie

Urządzenie jest zasilane z napięcia pojawiającego się po włączeniu stacyjki motocykla.

Schemat:

    instalacja motocykla
            |
            v
    +12 V po stacyjce
            |
            v
    przetwornica 12 V -> 5 V
            |
            v
          USB-C
            |
            v
       M5StickS3

Po wyłączeniu stacyjki zewnętrzne zasilanie urządzenia zanika.

M5StickS3 przełącza się na własny akumulator.

Akumulator jest wykorzystywany przede wszystkim do:

- zachowania działania urządzenia po wyłączeniu motocykla,
- zachowania wyników na ekranie,
- obsługi trzyminutowego okresu przed uzbrojeniem alarmu,
- pracy modułu alarmowego.

---

# 4. Nazwa urządzenia

Nazwa urządzenia:

**Motusy Moto Box**

Nazwa może być prezentowana na ekranie startowym oraz wykorzystywana w przyszłej komunikacji urządzenia.

---

# 5. Ekran startowy

Po każdym uruchomieniu urządzenia wyświetlany jest ekran startowy.

Ekran zawiera logo/nazwę:

**MOTUSY MOTO BOX**

Ekran startowy jest wyświetlany przez 5 sekund.

Po upływie 5 sekund urządzenie przechodzi do głównego ekranu parametrów.

---

# 6. Stany pracy urządzenia

Urządzenie może znajdować się w następujących stanach:

1. Start
2. Jazda
3. Okres po wyłączeniu motocykla
4. Tryb alarmowy
5. Kalibracja
6. Reset wyników

---

## 6.1. Start

Po pojawieniu się zasilania:

1. uruchamia się ESP32-S3,
2. inicjalizowany jest IMU,
3. odczytywane są dane zapisane w pamięci,
4. wyświetlane jest logo Motusy Moto Box przez 5 sekund,
5. rozpoczyna się nowa sesja jazdy,
6. wyniki "Ostatnia jazda" zostają wyzerowane,
7. alarm zostaje rozbrojony,
8. urządzenie przechodzi do trybu jazdy.

MAX OGÓLNIE pozostaje bez zmian.

---

# 7. Tryb jazdy

Tryb jazdy jest aktywowany po pojawieniu się zasilania z instalacji motocykla.

W tym stanie urządzenie:

- aktywnie odczytuje dane z BMI270,
- oblicza parametry dynamiki motocykla,
- aktualizuje wyniki bieżącej jazdy,
- aktualizuje rekordy ogólne,
- wyświetla wyniki,
- zapisuje wymagane dane w pamięci nieulotnej.

Każde ponowne pojawienie się zasilania po stacyjce rozpoczyna nową sesję jazdy.

---

# 8. Rejestrowane parametry

MVP rejestruje cztery podstawowe parametry maksymalne.

## 8.1. Maksymalny przechył w lewo

Największa wartość przechylenia motocykla w lewo osiągnięta podczas jazdy.

Wartość prezentowana w stopniach.

Przykład:

    LEWO: 32.4°

---

## 8.2. Maksymalny przechył w prawo

Największa wartość przechylenia motocykla w prawo osiągnięta podczas jazdy.

Wartość prezentowana w stopniach.

Przykład:

    PRAWO: 41.7°

---

## 8.3. Maksymalne przyspieszenie

Największa dodatnia wartość przyspieszenia w osi jazdy.

Podstawową jednostką zapisu jest g.

Przykład:

    PRZYSPIESZENIE: +0.63 g

W przyszłości wartość może być prezentowana również jako km/h/s.

---

## 8.4. Maksymalne hamowanie

Największa wartość ujemnego przyspieszenia w osi jazdy.

Podstawową jednostką zapisu jest g.

Przykład:

    HAMOWANIE: -0.82 g

W przyszłości wartość może być prezentowana również jako km/h/s.

---

# 9. Dwa niezależne zestawy wyników

Urządzenie przechowuje dwa zestawy wyników:

- MAX OGÓLNIE
- OSTATNIA JAZDA

Każdy zestaw zawiera:

- maksymalny przechył w lewo,
- maksymalny przechył w prawo,
- maksymalne przyspieszenie,
- maksymalne hamowanie.

Łącznie ekran prezentuje 8 wartości.

---

# 10. MAX OGÓLNIE

MAX OGÓLNIE przechowuje najlepsze/największe wartości osiągnięte od ostatniego ręcznego wyzerowania pomiarów.

MAX OGÓLNIE nie jest zerowane podczas:

- wyłączenia motocykla,
- rozpoczęcia nowej jazdy,
- restartu urządzenia,
- przejścia na zasilanie bateryjne.

MAX OGÓLNIE jest zerowane wyłącznie poprzez funkcję resetu wyników.

---

# 11. OSTATNIA JAZDA

OSTATNIA JAZDA przechowuje maksymalne wartości osiągnięte podczas aktualnej sesji jazdy.

Każde uruchomienie motocykla rozpoczyna nową sesję.

Przy rozpoczęciu nowej sesji:

- LEWO zostaje wyzerowane,
- PRAWO zostaje wyzerowane,
- HAMOWANIE zostaje wyzerowane,
- PRZYSPIESZENIE zostaje wyzerowane.

MAX OGÓLNIE pozostaje bez zmian.

Po wyłączeniu motocykla wyniki OSTATNIA JAZDA pozostają zachowane.

---

# 12. Aktualizacja wyników

Podczas jazdy urządzenie stale analizuje dane IMU.

Jeżeli nowa wartość przekroczy dotychczasowy rekord, odpowiednia wartość zostaje zaktualizowana.

Przykład:

    MAX PRAWO
    32.0°
    37.0°
    41.0°
    43.5°

Końcowa wartość:

    43.5°

Analogicznie działają:

- MAX LEWO,
- MAX PRZYSPIESZENIE,
- MAX HAMOWANIE.

MVP nie wymaga zapisywania pełnego przebiegu pomiarów.

Podstawowym zadaniem jest przechowywanie wartości maksymalnych.

---

# 13. Pamięć nieulotna

W pamięci nieulotnej urządzenie przechowuje co najmniej:

- MAX OGÓLNIE — 4 wartości,
- OSTATNIA JAZDA — 4 wartości,
- stan modułu alarmowego,
- parametry kalibracji IMU.

Dane muszą przetrwać:

- wyłączenie motocykla,
- restart urządzenia,
- utratę zewnętrznego zasilania.

---

# 14. Kalibracja IMU

Kalibracja określa stałą orientację urządzenia względem motocykla.

Kalibracja wykonywana jest po fizycznym zamontowaniu urządzenia.

Podczas kalibracji:

- motocykl musi znajdować się w pozycji referencyjnej,
- motocykl powinien być ustawiony pionowo,
- urządzenie powinno pozostawać nieruchome.

Urządzenie zbiera serię pomiarów i na ich podstawie określa pozycję odniesienia.

Po zakończeniu kalibracji parametry są zapisywane w pamięci nieulotnej.

Kalibracja pozostaje aktywna po:

- wyłączeniu urządzenia,
- restarcie,
- odłączeniu zewnętrznego zasilania.

Ponowna kalibracja zastępuje poprzednią kalibrację.

Kalibracja nie może być zmieniana podczas normalnej jazdy.

---

# 15. Reset wyników

Reset wyników powoduje wyzerowanie obu zestawów wyników:

- MAX OGÓLNIE,
- OSTATNIA JAZDA.

Reset nie zmienia:

- kalibracji IMU,
- ustawienia modułu alarmowego.

Po resecie urządzenie rozpoczyna zbieranie nowych rekordów od wartości zerowych.

---

# 16. Moduł alarmowy

Moduł alarmowy posiada dwa stany:

    ALARM WŁĄCZONY

oraz:

    ALARM WYŁĄCZONY

Stan modułu alarmowego jest zapisywany w pamięci nieulotnej.

Oznacza to, że ustawienie pozostaje aktywne po:

- wyłączeniu motocykla,
- restarcie urządzenia,
- ponownym uruchomieniu urządzenia.

---

# 17. Wyłączenie motocykla

Po zaniku zasilania zewnętrznego:

1. urządzenie przechodzi na zasilanie bateryjne,
2. wyniki pozostają dostępne,
3. rozpoczyna się odliczanie 3 minut,
4. urządzenie pozostaje aktywne.

Wyniki OSTATNIA JAZDA nie są zerowane.

MAX OGÓLNIE nie jest zmieniane.

---

# 18. Okres 3 minut po wyłączeniu

Przez 3 minuty od zaniku zasilania zewnętrznego urządzenie pozostaje w stanie oczekiwania.

W tym czasie:

- ekran może nadal prezentować wyniki,
- wyniki nie są zmieniane,
- moduł alarmowy nie jest jeszcze uzbrojony,
- urządzenie korzysta z własnego akumulatora.

Jeżeli w ciągu 3 minut ponownie pojawi się zasilanie po stacyjce:

- alarm zostaje rozbrojony,
- rozpoczyna się nowa jazda,
- OSTATNIA JAZDA zostaje wyzerowana,
- urządzenie przechodzi do trybu jazdy.

---

# 19. Tryb alarmowy

Po upływie 3 minut od wyłączenia motocykla urządzenie przechodzi do trybu alarmowego, jeżeli moduł alarmowy jest włączony.

W trybie alarmowym urządzenie wykorzystuje BMI270 do wykrywania poruszenia motocykla.

Algorytm powinien analizować między innymi:

- przyspieszenie,
- zmianę orientacji,
- czas trwania ruchu.

Pojedyncze, niewielkie drgania nie powinny powodować natychmiastowej eskalacji alarmu.

Dokładne progi wykrywania ruchu zostaną ustalone podczas testów.

---

# 20. Sygnalizacja alarmowa

Do lokalnej sygnalizacji alarmowej wykorzystywany jest wbudowany głośnik M5StickS3.

MVP przewiduje wielostopniową sygnalizację.

## Pierwsze naruszenie

Wykrycie ruchu powoduje krótki sygnał ostrzegawczy.

## Kolejne naruszenie lub utrzymujący się ruch

Urządzenie generuje dłuższy sygnał ostrzegawczy.

## Trzecie naruszenie lub dalszy ruch

Urządzenie przechodzi do ciągłej sygnalizacji dźwiękowej.

Dokładne czasy sygnałów oraz przerwy pomiędzy nimi zostaną ustalone podczas implementacji i testów.

Sygnalizacja ma przede wszystkim informować osobę poruszającą motocykl, że ruch został wykryty.

Nie jest wymagane, aby wbudowany głośnik pełnił funkcję profesjonalnej syreny alarmowej.

---

# 21. Automatyczne rozbrojenie alarmu

Pojawienie się zasilania po stacyjce oznacza uruchomienie motocykla.

W tym momencie:

- alarm zostaje natychmiast rozbrojony,
- rozpoczyna się nowa sesja jazdy,
- OSTATNIA JAZDA zostaje wyzerowana,
- rozpoczyna się rejestracja nowych wartości.

Nie jest wymagane ręczne rozbrajanie alarmu.

---

# 22. Sterowanie przyciskiem

Przycisk użytkownika posiada trzy funkcje zależne od czasu jego użycia.

## 22.1. Krótkie naciśnięcie

Krótkie naciśnięcie przełącza stan modułu alarmowego:

    ALARM WŁĄCZONY
            ↕
    ALARM WYŁĄCZONY

Nowy stan zostaje zapisany w pamięci nieulotnej.

Urządzenie powinno wyświetlić odpowiedni komunikat, np.:

    MODUŁ ALARMU WŁĄCZONY

lub:

    MODUŁ ALARMU WYŁĄCZONY

---

## 22.2. Przytrzymanie przez 3 sekundy

Przytrzymanie przycisku przez około 3 sekundy powoduje reset wszystkich wyników:

    MAX OGÓLNIE -> 0
    OSTATNIA JAZDA -> 0

Urządzenie powinno wyświetlić komunikat:

    POMIARY WYZEROWANE

Reset nie zmienia:

- kalibracji,
- ustawienia alarmu.

---

## 22.3. Przytrzymanie przez 10 sekund

Przytrzymanie przycisku przez około 10 sekund uruchamia procedurę kalibracji IMU.

Podczas kalibracji urządzenie powinno poinformować użytkownika o konieczności pozostawienia motocykla nieruchomo.

Po prawidłowym zakończeniu kalibracji urządzenie wyświetla:

    KALIBRACJA ZAKOŃCZONA

Nowa kalibracja zostaje zapisana w pamięci nieulotnej.

---

# 23. Priorytet długości naciśnięcia

Akcje przycisku muszą być rozpoznawane w sposób jednoznaczny.

Przytrzymanie przez 10 sekund nie może spowodować wcześniejszego wykonania funkcji resetu wyników po 3 sekundach.

Zasada:

- krótkie naciśnięcie -> przełączenie alarmu,
- około 3 sekundy -> reset wyników,
- około 10 sekund -> kalibracja.

Dłuższe przytrzymanie powinno wykonywać wyłącznie funkcję odpowiadającą najdłuższemu osiągniętemu progowi.

---

# 24. Wyświetlacz główny

Po zakończeniu ekranu startowego wyświetlacz prezentuje osiem podstawowych wartości.

## MAX OGÓLNIE

- LEWO
- PRAWO
- HAMOWANIE
- PRZYSPIESZENIE

## OSTATNIA JAZDA

- LEWO
- PRAWO
- HAMOWANIE
- PRZYSPIESZENIE

Dodatkowo ekran może prezentować stan urządzenia:

- rejestracja,
- alarm,
- kalibracja,
- reset wyników.

---

# 25. Zachowanie po wyłączeniu motocykla

Po wyłączeniu motocykla:

- wyniki ostatniej jazdy pozostają dostępne,
- MAX OGÓLNIE pozostaje dostępne,
- urządzenie przechodzi na akumulator,
- rozpoczyna się trzyminutowe odliczanie,
- po 3 minutach może zostać uzbrojony moduł alarmowy.

Urządzenie nie wymaga żadnej czynności użytkownika.

---

# 26. Zachowanie po ponownym włączeniu motocykla

Po ponownym pojawieniu się zasilania:

1. alarm zostaje rozbrojony,
2. rozpoczyna się nowa sesja jazdy,
3. OSTATNIA JAZDA zostaje wyzerowana,
4. MAX OGÓLNIE pozostaje bez zmian,
5. rozpoczyna się rejestracja nowych wartości.

---

# 27. Odporność na utratę zasilania

Dane zapisane w pamięci nieulotnej nie mogą zależeć od obecności zewnętrznego zasilania.

W szczególności nie mogą zostać utracone:

- MAX OGÓLNIE,
- OSTATNIA JAZDA,
- kalibracja,
- stan modułu alarmowego.

---

# 28. Zakres MVP

MVP obejmuje:

- M5StickS3,
- ESP32-S3,
- BMI270,
- ekran,
- przycisk,
- głośnik,
- akumulator,
- zasilanie 12 V -> 5 V USB-C,
- automatyczne rozpoczęcie pracy po włączeniu motocykla,
- pomiar przechyłu,
- pomiar przyspieszenia,
- pomiar hamowania,
- MAX OGÓLNIE,
- OSTATNIA JAZDA,
- pamięć nieulotną,
- kalibrację pozycji montażowej,
- reset wyników,
- automatyczne przejście na baterię po wyłączeniu motocykla,
- trzyminutowe opóźnienie przed uzbrojeniem alarmu,
- automatyczne rozbrojenie alarmu po włączeniu motocykla,
- prostą sygnalizację ruchu przez wbudowany głośnik.

---

# 29. Poza zakresem MVP

W MVP nie przewiduje się:

- GPS,
- pomiaru rzeczywistej prędkości,
- Bluetooth z telefonem,
- aplikacji mobilnej,
- komunikacji z API,
- zapisu pełnej historii trasy,
- zewnętrznej syreny,
- dodatkowych czujników,
- zdalnego sterowania,
- chmury,
- panelu WWW.

Funkcje te mogą zostać dodane w kolejnych wersjach.

---

# 30. Główna zasada projektu

Motusy Moto Box ma być urządzeniem autonomicznym i bezobsługowym.

Normalny cykl działania:

    STACYJKA ON
        |
        v
    START URZĄDZENIA
        |
        v
    LOGO 5 SEKUND
        |
        v
    NOWA JAZDA
        |
        v
    RESET OSTATNIEJ JAZDY
        |
        v
    REJESTRACJA IMU
        |
        +----> aktualizacja OSTATNIA JAZDA
        |
        +----> aktualizacja MAX OGÓLNIE
        |
        v
    STACYJKA OFF
        |
        v
    PRZEJŚCIE NA BATERIĘ
        |
        v
    3 MINUTY OCZEKIWANIA
        |
        v
    ALARM
        |
        v
    WYKRYCIE RUCHU
        |
        v
    SYGNALIZACJA
        |
        v
    STACYJKA ON
        |
        v
    ROZBROJENIE ALARMU
        |
        v
    NOWA JAZDA

---

# 31. Założenie projektowe

Pierwszym celem MVP jest potwierdzenie, że M5StickS3 oraz BMI270 pozwalają wiarygodnie określać:

- maksymalny przechył w lewo,
- maksymalny przechył w prawo,
- maksymalne przyspieszenie,
- maksymalne hamowanie

podczas rzeczywistej jazdy motocyklem.

Dopiero po potwierdzeniu poprawnego działania podstawowego pomiaru należy rozwijać kolejne funkcje urządzenia.