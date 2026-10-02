#include "gl.h"
#include "font.h"

// The vanilla font is 8 pixels tall, which is the calculator's GUI scale. A
// desktop runs the same front-end at twice that in both directions, so it gets
// the doubled atlas and the doubled advances -- the same thing vanilla does when
// the GUI scale goes up. Both pairs define `font_bmp` and `font_dat`, so exactly
// one of them is included here.
#ifdef _TINSPIRE
#include "textures/font_dat.h"
#include "textures/font_bmp.h"
#else
#include "textures/font_dat_wide.h"
#include "textures/font_bmp_wide.h"
#endif

static inline int drawChar(char c, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y)
{
    const uint32_t glyph_w = font_dat[8];
    const uint32_t height = font_dat[12];

    // The atlas starts at font_dat[16]; a character below it (or a byte with the
    // top bit set) would index outside the table, so the code is taken unsigned.
    const unsigned char code = static_cast<unsigned char>(c);
    const unsigned int pos = code - font_dat[16];
    const unsigned int cols = font_bmp.width / font_dat[8];
    unsigned int pos_x = (pos % cols) * glyph_w;
    unsigned int pos_y = (pos / cols) * height;

    //Each character has its specific width
    const unsigned int width = font_dat[code + 17];

    // A glyph that is partly off the texture (a centred line wider than the
    // screen, a label at the very edge) is clipped rather than written past the
    // end of the buffer: the text is drawn from unsigned coordinates, and one
    // that is a little too long used to corrupt whatever followed the framebuffer.
    const bool fully_inside = x + width <= tex.width && y + height <= tex.height;
    if(fully_inside)
    {
        for(unsigned int x1 = 0; x1 < width; x1++)
            for(unsigned int y1 = 0; y1 < height; y1++)
            {
                if(font_bmp.bitmap[pos_x + x1 + (pos_y + y1) * font_bmp.width] == 0xFFFF)
                    tex.bitmap[x + x1 + (y + y1) * tex.width] = color;
            }

        return width;
    }

    for(unsigned int x1 = 0; x1 < width; x1++)
    {
        if(x + x1 >= tex.width)
            break; // the rest of this glyph is off the right edge

        for(unsigned int y1 = 0; y1 < height; y1++)
        {
            if(y + y1 >= tex.height)
                break; // and so is this column's tail off the bottom

            if(font_bmp.bitmap[pos_x + x1 + (pos_y + y1) * font_bmp.width] == 0xFFFF)
                tex.bitmap[x + x1 + (y + y1) * tex.width] = color;
        }
    }

    return width;
}

void drawStringCenter(const char *str, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y)
{
    // A line wider than the distance to the edge starts at the edge rather than
    // wrapping round the unsigned centre, which is what a clipped title means.
    const unsigned int half = measureString(str) / 2;
    drawString(str, color, tex, half >= x ? 0 : x - half, y);
}

unsigned int measureString(const char *str)
{
    unsigned int width = 0;
    while(*str)
        width += font_dat[17 + static_cast<unsigned char>(*str++)];
    return width;
}

void drawString(const char *str, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y)
{
    const char *ptr = str;
    const unsigned int start_x = x;
    while(*ptr)
    {
        if(*ptr == '\n')
        {
            x = start_x;
            y += fontHeight();
        }
        else if(*ptr == '\t')
        {
            x += 32;
            x -= x % 32;
        }
        else
            x += drawChar(*ptr, color, tex, x, y);

        ++ptr;
    }
}

/*
 * One glyph, magnified: every opaque source pixel is written as a
 * `scale` x `scale` block, which is the nearest-neighbour upscale a bitmap font
 * wants. It shares drawChar()'s metrics and its clipping, so a scaled line that
 * runs off the texture is cut off at the edge rather than written past the end.
 */
static inline unsigned int drawCharScaled(char c, COLOR color, TEXTURE &tex, unsigned int x,
                                          unsigned int y, unsigned int scale)
{
    const uint32_t glyph_w = font_dat[8];
    const uint32_t height = font_dat[12];

    const unsigned char code = static_cast<unsigned char>(c);
    const unsigned int pos = code - font_dat[16];
    const unsigned int cols = font_bmp.width / font_dat[8];
    const unsigned int pos_x = (pos % cols) * glyph_w;
    const unsigned int pos_y = (pos / cols) * height;
    const unsigned int width = font_dat[code + 17];

    for(unsigned int x1 = 0; x1 < width; ++x1)
    {
        for(unsigned int y1 = 0; y1 < height; ++y1)
        {
            if(font_bmp.bitmap[pos_x + x1 + (pos_y + y1) * font_bmp.width] != 0xFFFF)
                continue;

            for(unsigned int by = 0; by < scale; ++by)
            {
                const unsigned int py = y + y1 * scale + by;
                if(py >= tex.height)
                    continue;

                for(unsigned int bx = 0; bx < scale; ++bx)
                {
                    const unsigned int px = x + x1 * scale + bx;
                    if(px >= tex.width)
                        continue;

                    tex.bitmap[px + py * tex.width] = color;
                }
            }
        }
    }

    return width * scale;
}

void drawStringScaled(const char *str, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y,
                      unsigned int scale)
{
    if(scale == 0)
        scale = 1;

    // One is the ordinary draw: there is no reason to pay for the block loop.
    if(scale == 1)
    {
        drawString(str, color, tex, x, y);
        return;
    }

    const char *ptr = str;
    const unsigned int start_x = x;
    while(*ptr)
    {
        if(*ptr == '\n')
        {
            x = start_x;
            y += fontHeight() * scale;
        }
        else if(*ptr == '\t')
        {
            // The tab stop is a whole block of the doubled cell, so the columns
            // stay aligned whatever the scale is.
            x += 32 * scale;
            x -= x % (32 * scale);
        }
        else
            x += drawCharScaled(*ptr, color, tex, x, y, scale);

        ++ptr;
    }
}

void drawStringCenterScaled(const char *str, COLOR color, TEXTURE &tex, unsigned int x,
                           unsigned int y, unsigned int scale)
{
    const unsigned int half = measureStringScaled(str, scale) / 2;
    drawStringScaled(str, color, tex, half >= x ? 0 : x - half, y, scale);
}

unsigned int measureStringScaled(const char *str, unsigned int scale)
{
    if(scale == 0)
        scale = 1;
    return measureString(str) * scale;
}

unsigned int fontHeight()
{
    return font_dat[12];
}
