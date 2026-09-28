#ifndef FONT_H
#define FONT_H

#include "gl.h"

void drawStringCenter(const char *str, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y);
void drawString(const char *str, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y);
/**
 * The width the string would take, in pixels at scale 1. The title screen needs
 * it to centre a logo and a button label before either is drawn.
 */
unsigned int measureString(const char *str);
/**
 * drawString() at a whole-number scale: every glyph pixel becomes a scale x scale
 * block. This is how the title screen draws its wordmark out of the same 14-pixel
 * font as the rest of the game rather than shipping a second bitmap of text.
 *
 * Unlike drawString(), the scaled version never writes outside the texture: a
 * logo is wide enough to run off the edge of a calculator screen, and clipping it
 * is the caller's expectation rather than a corrupt frame.
 */
unsigned int drawStringScaled(const char *str, COLOR color, TEXTURE &tex,
                              unsigned int x, unsigned int y, unsigned int scale);
unsigned int fontHeight();

#endif // FONT_H
