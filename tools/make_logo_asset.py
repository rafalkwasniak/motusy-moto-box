#!/usr/bin/env python3
"""Motusy Moto Box - generator zasobu logo dla firmware.

Zrodlo (docs/logo.png) to grafika 1000x427 z przezroczystym tlem: czarna tablica
i biala grafika. Ekran urzadzenia ma 240x135 i tlo jest czarne, wiec skladamy
logo na czarnym tle, skalujemy do szerokosci ekranu i zapisujemy jako PNG
w skali szarosci - M5GFX dekoduje go w locie przez lcd.drawPng().

PNG zamiast surowej bitmapy, bo:
  - zachowuje antyaliasing (drobne detale czaszki przy 1 bpp rozsypuja sie),
  - line art kompresuje sie swietnie (kilka kB zamiast 50 kB RGB565).

Uzycie:
    python3 tools/make_logo_asset.py [szerokosc]
"""

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "docs" / "logo.png"
OUTPUT_HEADER = ROOT / "src" / "assets" / "logo_asset.h"
OUTPUT_PREVIEW = ROOT / "tools" / "logo_preview.png"

DEFAULT_WIDTH = 236


def build(width: int) -> None:
    source = Image.open(SOURCE).convert("RGBA")

    # Skladanie na czarnym tle: przezroczyste otoczenie tablicy zlewa sie
    # z tlem ekranu, wiec logo "wtapia sie" zamiast miec ramke.
    canvas = Image.new("RGBA", source.size, (0, 0, 0, 255))
    canvas.alpha_composite(source)

    height = round(width * source.height / source.width)
    resized = canvas.convert("L").resize((width, height), Image.LANCZOS)

    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    resized.save(OUTPUT_PREVIEW)
    resized.save(OUTPUT_PREVIEW.with_suffix(".tmp.png"), optimize=True)
    payload = OUTPUT_PREVIEW.with_suffix(".tmp.png").read_bytes()
    OUTPUT_PREVIEW.with_suffix(".tmp.png").unlink()

    lines = [
        "// PLIK GENEROWANY AUTOMATYCZNIE - nie edytowac recznie.",
        "//",
        "// Zrodlo:    docs/logo.png",
        "// Generator: tools/make_logo_asset.py",
        f"// Rozmiar:   {width}x{height} px, PNG w skali szarosci, {len(payload)} bajtow",
        "",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace assets {",
        "",
        f"constexpr int kLogoWidth = {width};",
        f"constexpr int kLogoHeight = {height};",
        f"constexpr uint32_t kLogoPngLength = {len(payload)};",
        "",
        "const uint8_t kLogoPng[] = {",
    ]

    for offset in range(0, len(payload), 16):
        chunk = payload[offset:offset + 16]
        lines.append("    " + " ".join(f"0x{byte:02X}," for byte in chunk))

    lines += [
        "};",
        "",
        "}  // namespace assets",
        "",
    ]

    OUTPUT_HEADER.write_text("\n".join(lines))

    print(f"logo:    {width}x{height} px")
    print(f"PNG:     {len(payload)} bajtow")
    print(f"naglowek:{OUTPUT_HEADER.relative_to(ROOT)}")
    print(f"podglad: {OUTPUT_PREVIEW.relative_to(ROOT)}")


if __name__ == "__main__":
    build(int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_WIDTH)
