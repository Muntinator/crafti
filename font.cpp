#include "gl.h"
#include "font.h"

#include "textures/font_dat.h"
#include "textures/font_bmp.h"

static inline int drawChar(char c, COLOR color, TEXTURE &tex, unsigned int x, unsigned int y)
{
    const uint32_t glyph_w = font_dat[8];
    const uint32_t height = font_dat[12];

    //font_dat[16] is the char at the top left
    const unsigned int pos = c - font_dat[16];
    const unsigned int cols = font_bmp.width / font_dat[8];
    unsigned int pos_x = (pos % cols) * glyph_w;
    unsigned int pos_y = (pos / cols) * height;

    //Each character has its specific width
    const unsigned int width = font_dat[c + 17];

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

unsigned int drawStringScaled(const char *str, COLOR color, TEXTURE &tex,
                              unsigned int x, unsigned int y, unsigned int scale)
{
    if(scale == 0)
        return 0;

    const unsigned int height = font_dat[12];
    const unsigned int cols = font_bmp.width / font_dat[8];
    const unsigned int start_x = x;

    while(*str)
    {
        const unsigned char c = static_cast<unsigned char>(*str++);

        if(c == '\n')
        {
            x = start_x;
            y += height * scale;
            continue;
        }

        const unsigned int pos = c - font_dat[16];
        const unsigned int glyph_x = (pos % cols) * font_dat[8];
        const unsigned int glyph_y = (pos / cols) * height;
        const unsigned int width = font_dat[c + 17];

        for(unsigned int gx = 0; gx < width; ++gx)
        {
            for(unsigned int gy = 0; gy < height; ++gy)
            {
                if(font_bmp.bitmap[glyph_x + gx + (glyph_y + gy) * font_bmp.width] != 0xFFFF)
                    continue;

                for(unsigned int sy = 0; sy < scale; ++sy)
                    for(unsigned int sx = 0; sx < scale; ++sx)
                    {
                        const unsigned int px = (x + gx) * scale + sx;
                        const unsigned int py = (y + gy) * scale + sy;
                        if(px < tex.width && py < tex.height)
                            tex.bitmap[px + py * tex.width] = color;
                    }
            }
        }

        x += width;
    }

    return (x - start_x) * scale;
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

unsigned int fontHeight()
{
    return font_dat[12];
}
