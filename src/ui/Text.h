// Motusy Moto Box — napisy dopasowujace sie do szerokosci ekranu.
//
// Ekran ma 240 px, a komunikaty typu "POMIARY WYZEROWANE" albo
// "NIEUDANA - SPROBUJ PONOWNIE" nie miesza sie w duzym foncie i wychodza poza
// krawedz. Zamiast dobierac font recznie do kazdego napisu — co i tak rozjezdza
// sie przy pierwszej zmianie tekstu — mierzymy napis i schodzimy o oczko nizej,
// az sie zmiesci.

#pragma once

#include <M5Unified.h>

namespace ui {
namespace text {

/// Rysuje napis najwiekszym fontem, ktory miesci sie w `maxWidth`.
/// Datum i kolor ustawia wywolujacy. Gdy nawet najmniejszy font nie wystarcza,
/// napis jest rysowany i tak — lepiej przyciety niz zaden.
/// @return wysokosc uzytego fontu [px], przydatna do ulozenia kolejnej linii
int drawFitted(m5gfx::LovyanGFX* gfx, const char* str, int x, int y, int maxWidth);

/// Sama wysokosc fontu, ktory zostalby uzyty — bez rysowania.
int fittedHeight(m5gfx::LovyanGFX* gfx, const char* str, int maxWidth);

}  // namespace text
}  // namespace ui
