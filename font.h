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
unsigned int fontHeight();

/**
 * The same text, magnified by a whole number. Vanilla draws the death screen's
 * "You Died!" at twice the GUI scale, and its font is the same 8-pixel atlas at
 * any size, so the two functions below are the scaled counterparts of the pair
 * above: every source pixel becomes an `scale` x `scale` block. A scale of 1 is
 * the ordinary draw.
 */
void drawStringScaled(const char *str, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y,
                      unsigned int scale);
void drawStringCenterScaled(const char *str, COLOR color, TEXTURE &tex, unsigned int x,
                           unsigned int y, unsigned int scale);
/** The width of scaled text, which is measureString() times the scale. */
unsigned int measureStringScaled(const char *str, unsigned int scale);

#endif // FONT_H
