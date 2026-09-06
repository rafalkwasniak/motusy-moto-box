#!/usr/bin/env python3
"""Szum rozowy do kalibracji pomiaru halasu (docs/pomiar-halasu.md §8, etap N2).

DLACZEGO SZUM ROZOWY, A NIE MUZYKA ANI TELEWIZOR. Miernik wzorcowy GM1351
odswieza odczyt dwa razy na sekunde. Zrodlo, ktore skacze — mowa, muzyka,
koty — daje na nim wartosc, ktorej nie da sie odczytac powtarzalnie, a roznica
miedzy dwoma przyrzadami utonie w tym skakaniu. Wzorzec musi byc USTALONY.

DLACZEGO ROZOWY, A NIE BIALY. Szum bialy ma rowna moc na herc, czyli polowa
jego energii siedzi w najwyzszej oktawie — a tam wazenie A i tor akustyczny
obudowy robia najwiecej. Rozowy ma rowna moc na oktawe, czyli rozklada
energie tak, jak robia to zrodla naturalne (i wydech motocykla).

DLACZEGO TRZY POZIOMY, A NIE JEDEN. Jeden punkt daje przesuniecie. Dopiero
kilka punktow pokazuje NACHYLENIE, a ono musi wyjsc 1,0. Jesli nie wychodzi,
to jest DIAGNOSTYKA SPRZETU, a nie parametr do dopasowania — szukaj ALC,
bramki szumow albo przesterowania. Nachylenie 1,36 w pierwszym projekcie
wzielo sie dokladnie z ulegniecia tej pokusie.

Uzycie:
    python3 tools/szum_rozowy.py                 # zapis szum_rozowy.wav
    python3 tools/szum_rozowy.py --sekundy 30
"""

import argparse
import math
import random
import struct
import wave

SAMPLE_RATE = 48000


def pink(count, seed=20250906):
    """Szum rozowy metoda Vossa-McCartneya.

    Suma kilkunastu zrodel bialych, kazde odswiezane dwa razy rzadziej od
    poprzedniego. Daje charakterystyke -3 dB na oktawe bez projektowania
    filtru, a przy okazji jest odtwarzalna: ten sam seed to ten sam plik,
    wiec dwa pomiary z roznych dni porownuja sie ze soba.
    """
    rng = random.Random(seed)
    rows = 16
    values = [rng.uniform(-1.0, 1.0) for _ in range(rows)]
    total = sum(values)

    out = []
    for index in range(1, count + 1):
        # Ktore zrodlo odswiezyc: numer najnizszego zapalonego bitu licznika.
        bit = index & -index
        row = bit.bit_length() - 1
        if row < rows:
            fresh = rng.uniform(-1.0, 1.0)
            total += fresh - values[row]
            values[row] = fresh
        out.append(total / rows)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sekundy", type=float, default=25.0)
    parser.add_argument("--plik", default="szum_rozowy.wav")
    parser.add_argument(
        "--szczyt",
        type=float,
        default=0.5,
        help="amplituda szczytowa 0-1; nizej niz 1, bo szum ma wysoki wspolczynnik szczytu",
    )
    args = parser.parse_args()

    count = int(SAMPLE_RATE * args.sekundy)
    samples = pink(count)

    peak = max(abs(value) for value in samples) or 1.0
    scale = args.szczyt / peak

    # Narastanie i opadanie po 50 ms: skok od ciszy do pelnej mocy jest
    # trzaskiem, czyli dokladnie tym, co miernik ma odrzucac.
    fade = int(SAMPLE_RATE * 0.05)
    frames = bytearray()
    for index, value in enumerate(samples):
        envelope = 1.0
        if index < fade:
            envelope = index / fade
        elif index > count - fade:
            envelope = max(0.0, (count - index) / fade)
        amplitude = max(-1.0, min(1.0, value * scale * envelope))
        frames += struct.pack("<h", int(amplitude * 32767))

    with wave.open(args.plik, "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(SAMPLE_RATE)
        handle.writeframes(bytes(frames))

    rms = math.sqrt(sum((v * scale) ** 2 for v in samples) / count)
    print(f"{args.plik}: {args.sekundy:.0f} s, {SAMPLE_RATE} Hz")
    print(f"szczyt {args.szczyt:.2f}, RMS {rms:.4f} ({20 * math.log10(rms):.1f} dBFS)")


if __name__ == "__main__":
    main()
