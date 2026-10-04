// Host test for nGL's GL_LINES path (nGL/gl.cpp).
//
// The block selection outline is drawn the way vanilla draws it: no texture, a
// one pixel wireframe. That path only exists because GL_LINES was added to
// GLDrawMode and dispatched to nglDrawLine3D, so it needs a test of its own --
// nothing else in the engine draws lines.
//
// Two things are worth pinning down:
//
//   * a GL_LINES pair really reaches the framebuffer, one pixel wide, in the
//     colour the vertex was given, with its neighbours untouched; and
//   * a segment that is vertical *on screen* -- which every world-vertical edge
//     of a block outline is the moment a block faces the player squarely -- does
//     not divide by zero. Fix::operator/ is plain integer division, so that used
//     to be a divide-by-zero trap rather than a line.
//
// Every expectation below is derived from W/H through screen_x()/screen_y()
// rather than written out. That matters: the host build is not _TINSPIRE, so
// gl.h gives SCREEN_WIDTH/HEIGHT as 640x480 where the calculator gets 320x240,
// and nglPerspective offsets by SCREEN_WIDTH/2 and SCREEN_HEIGHT/2. A first
// version of this test hardcoded the calculator's numbers and failed every
// geometry check while the rasteriser was working perfectly -- the segments were
// being drawn, 160 pixels away from where the test was looking.
//
// The last frame is written to build/nglines_test.ppm so the segments can be
// looked at rather than only counted.
//
// Build and run with `make -C tests`.

#include "../tools/pcsim/SDL/SDL.h"

#include "gl.h"

#include <stdio.h>

static int failures = 0;
static int checks = 0;

#define CHECK(condition) do { ++checks; if(!(condition)) { \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while(0)

namespace
{

const int W = SCREEN_WIDTH;
const int H = SCREEN_HEIGHT;
const COLOR WHITE = 0xFFFF;
const COLOR BLACK = 0x0000;

COLOR *frame = nullptr;

// The geometry is drawn at z == the near plane, where perspectiveDivisor()
// returns exactly the near plane and the perspective divide is 1. ngl's default
// near plane is 256.
const int NEAR = 256;

// nglPerspective: x += SCREEN_WIDTH/2, y += SCREEN_HEIGHT/2,
// y = SCREEN_HEIGHT - 1 - y.
int screen_x(const int x) { return x + W/2; }
int screen_y(const int y) { return (H - 1) - (y + H/2); }

void reset()
{
    glColor3f(1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for(int i = 0; i < W*H; ++i)
        frame[i] = WHITE;
}

COLOR at(const int x, const int y)
{
    if(x < 0 || y < 0 || x >= W || y >= H)
        return WHITE;
    return frame[y*W + x];
}

int black_count()
{
    int n = 0;
    for(int i = 0; i < W*H; ++i)
        if(frame[i] == BLACK)
            ++n;
    return n;
}

// Bounding box of every black pixel, as x0/y0/x1/y1. Returns false for an empty
// frame.
struct Box { int x0, y0, x1, y1; };

bool black_bbox(Box &box)
{
    box.x0 = W; box.y0 = H; box.x1 = -1; box.y1 = -1;

    for(int y = 0; y < H; ++y)
        for(int x = 0; x < W; ++x)
            if(frame[y*W + x] == BLACK)
            {
                if(x < box.x0) box.x0 = x;
                if(x > box.x1) box.x1 = x;
                if(y < box.y0) box.y0 = y;
                if(y > box.y1) box.y1 = y;
            }

    return box.x1 >= box.x0;
}

// A horizontal segment: the shallow branch of nglDrawLine3D.
void testHorizontalLineIsOnePixel()
{
    reset();
    glColor3f(0, 0, 0);

    glBegin(GL_LINES);
    glVertex3f(GLFix(-100), GLFix(0), GLFix(NEAR));
    glVertex3f(GLFix(-50), GLFix(0), GLFix(NEAR));
    glEnd();

    const int y = screen_y(0);
    const int x0 = screen_x(-100), x1 = screen_x(-50);

    int drawn = 0;
    for(int x = x0; x <= x1; ++x)
        if(at(x, y) == BLACK)
            ++drawn;

    CHECK(drawn == x1 - x0 + 1);   // the whole segment, inclusive of both ends
    CHECK(at(x0 - 1, y) == WHITE); // and nothing either side of it
    CHECK(at(x1 + 1, y) == WHITE);

    // One pixel tall: the rows above and below are untouched.
    int above = 0, below = 0;
    for(int x = x0; x <= x1; ++x)
    {
        if(at(x, y - 1) == BLACK) ++above;
        if(at(x, y + 1) == BLACK) ++below;
    }
    CHECK(above == 0);
    CHECK(below == 0);
}

// The case that used to trap: diff_x == 0, so the slope was a divide by zero.
void testScreenVerticalLineDoesNotDivideByZero()
{
    reset();
    glColor3f(0, 0, 0);

    glBegin(GL_LINES);
    glVertex3f(GLFix(0), GLFix(-50), GLFix(NEAR));
    glVertex3f(GLFix(0), GLFix(-100), GLFix(NEAR));
    glEnd();

    const int x = screen_x(0);
    // screen_y inverts y, so the far end (-100) is the larger row.
    const int y0 = screen_y(-50), y1 = screen_y(-100);

    int drawn = 0;
    for(int y = y0; y <= y1; ++y)
        if(at(x, y) == BLACK)
            ++drawn;

    CHECK(drawn == y1 - y0 + 1);

    // Still one pixel wide.
    int left = 0, right = 0;
    for(int y = y0; y <= y1; ++y)
    {
        if(at(x - 1, y) == BLACK) ++left;
        if(at(x + 1, y) == BLACK) ++right;
    }
    CHECK(left == 0);
    CHECK(right == 0);
}

// A diagonal, which takes whichever branch the dominant-axis test picks.
void testDiagonalLine()
{
    reset();
    glColor3f(0, 0, 0);

    glBegin(GL_LINES);
    glVertex3f(GLFix(-100), GLFix(0), GLFix(NEAR));
    glVertex3f(GLFix(0), GLFix(-100), GLFix(NEAR));
    glEnd();

    Box box;
    CHECK(black_bbox(box));
    CHECK(box.x0 == screen_x(-100) && box.x1 == screen_x(0));
    // screen_y inverts, so the world y = -100 end is the *larger* row.
    CHECK(box.y0 == screen_y(0) && box.y1 == screen_y(-100));

    // Both ends land on a pixel.
    CHECK(at(screen_x(-100), screen_y(0)) == BLACK);
    CHECK(at(screen_x(0), screen_y(-100)) == BLACK);

    // The steep branch takes the dominant axis (y here, 100 against 100 with
    // >=-free strict > sending equal extents to the shallow branch), so the
    // segment is 101 pixels long either way -- the whole diagonal, not a
    // Bresenham thinning of it.
    CHECK(black_count() == 101);
}

// Two disjoint pairs in one glBegin/glEnd must not be joined into a strip.
void testPairsAreDisjoint()
{
    reset();
    glColor3f(0, 0, 0);

    glBegin(GL_LINES);
    // pair one, along y = 0
    glVertex3f(GLFix(-100), GLFix(0), GLFix(NEAR));
    glVertex3f(GLFix(-50), GLFix(0), GLFix(NEAR));
    // pair two, 60 further down in world y
    glVertex3f(GLFix(-100), GLFix(-60), GLFix(NEAR));
    glVertex3f(GLFix(-50), GLFix(-60), GLFix(NEAR));
    glEnd();

    const int x = screen_x(-75);
    CHECK(at(x, screen_y(0)) == BLACK);
    CHECK(at(x, screen_y(-60)) == BLACK);

    // and nothing joining the two
    CHECK(at(x, screen_y(-30)) == WHITE);

    CHECK(black_count() == 2*51);
}

// A degenerate segment has no screen length at all; it must draw one pixel and
// return rather than dividing by zero twice.
void testDegenerateSegment()
{
    reset();
    glColor3f(0, 0, 0);

    glBegin(GL_LINES);
    glVertex3f(GLFix(-100), GLFix(0), GLFix(NEAR));
    glVertex3f(GLFix(-100), GLFix(0), GLFix(NEAR));
    glEnd();

    CHECK(black_count() == 1);
    CHECK(at(screen_x(-100), screen_y(0)) == BLACK);
}

// The colour of a vertex has to reach the framebuffer untouched: the outline is
// drawn black, and this is what keeps it black rather than white.
void testVertexColourReachesTheFramebuffer()
{
    reset();
    glColor3f(0, 0, 1);   // pure blue in RGB565

    glBegin(GL_LINES);
    glVertex3f(GLFix(-100), GLFix(0), GLFix(NEAR));
    glVertex3f(GLFix(-50), GLFix(0), GLFix(NEAR));
    glEnd();

    const COLOR blue = 0b11111;   // r=0, g=0, b=31
    CHECK(at(screen_x(-75), screen_y(0)) == blue);
    CHECK(black_count() == 0);

    // A line must not be able to overwrite itself: the z test rejects the
    // second write at the same depth, and depth has to advance for a nearer
    // line to win.
    glColor3f(1, 1, 1);
    glBegin(GL_LINES);
    glVertex3f(GLFix(-75), GLFix(0), GLFix(NEAR));
    glVertex3f(GLFix(-75), GLFix(0), GLFix(NEAR));
    glEnd();
    CHECK(at(screen_x(-75), screen_y(0)) == blue);
}

// The frame that gets written out: a twelve edge wireframe box, which is what
// worldtask.cpp's selection outline is -- vanilla draws the selection box as
// twelve line segments over a POSITION-only vertex format, with no texture.
void drawWireframeBox()
{
    reset();
    glColor3f(0, 0, 0);

    // A real box, not a flat square: the back face sits nearer the camera so the
    // perspective divide magnifies it and the silhouette is the hexahedron a
    // block outline reads as.
    const GLFix lo(-80), hi(-20);
    const GLFix NEARZ(NEAR), FARZ(NEAR - 60);

    glBegin(GL_LINES);
    glVertex3f(lo, lo, NEARZ); glVertex3f(hi, lo, NEARZ);   // front top
    glVertex3f(hi, lo, NEARZ); glVertex3f(hi, hi, NEARZ);   // front right
    glVertex3f(hi, hi, NEARZ); glVertex3f(lo, hi, NEARZ);   // front bottom
    glVertex3f(lo, hi, NEARZ); glVertex3f(lo, lo, NEARZ);   // front left
    glVertex3f(lo, lo, FARZ);  glVertex3f(hi, lo, FARZ);    // back top
    glVertex3f(hi, lo, FARZ);  glVertex3f(hi, hi, FARZ);    // back right
    glVertex3f(hi, hi, FARZ);  glVertex3f(lo, hi, FARZ);    // back bottom
    glVertex3f(lo, hi, FARZ);  glVertex3f(lo, lo, FARZ);    // back left
    glVertex3f(lo, lo, NEARZ); glVertex3f(lo, lo, FARZ);     // the four joins
    glVertex3f(hi, lo, NEARZ); glVertex3f(hi, lo, FARZ);
    glVertex3f(hi, hi, NEARZ); glVertex3f(hi, hi, FARZ);
    glVertex3f(lo, hi, NEARZ); glVertex3f(lo, hi, FARZ);
    glVertex3f(lo, lo, FARZ);  glVertex3f(lo, lo, FARZ);     // degenerate, still one pixel
    glEnd();

    Box box;
    CHECK(black_bbox(box));

    // Twelve edges, no filled faces: far fewer black pixels than the box's area.
    const int area = (box.x1 - box.x0 + 1)*(box.y1 - box.y0 + 1);
    CHECK(black_count() < area/2);

    // The perspective divide magnifies about the centre of the screen, and this
    // box's coordinates are all negative, so the nearer back face is pushed
    // further *left* and further *down* only. It therefore overshoots the front
    // face on two sides and leaves the other two pinned to the front face's own
    // extremes.
    CHECK(box.x0 < screen_x(lo));
    CHECK(box.y1 > screen_y(lo));
    CHECK(box.x1 == screen_x(hi));
    CHECK(box.y0 == screen_y(hi));

    // And the whole thing stayed inside the frame.
    CHECK(box.x0 >= 0 && box.x1 < W && box.y0 >= 0 && box.y1 < H);

    // All eight corners of the box, front and back, are inked. nGL's projection is
    // div = near_plane/z in 12 bit fixed point, then the offset and the y flip,
    // and only the final result is truncated to a pixel. Reproducing that order
    // here is not worth the coupling, and on negative coordinates truncating a
    // per-axis intermediate instead lands a pixel away from what the rasteriser
    // used, so look for ink in a one pixel neighbourhood.
    int corners = 0;
    const GLFix zs[2] = { NEARZ, FARZ };
    for(int f = 0; f < 2; ++f)
    {
        const GLFix div(Fix<12, int32_t>(GLFix(NEAR))/zs[f].toInteger<int>());

        for(int c = 0; c < 4; ++c)
        {
            const int wx = (c == 0 || c == 3) ? lo.toInteger<int>() : hi.toInteger<int>();
            const int wy = (c < 2) ? lo.toInteger<int>() : hi.toInteger<int>();

            const int sx = screen_x(GLFix(div*wx).toInteger<int>());
            const int sy = screen_y(GLFix(div*wy).toInteger<int>());

            bool found = false;
            for(int dy = -1; dy <= 1 && !found; ++dy)
                for(int dx = -1; dx <= 1 && !found; ++dx)
                    if(at(sx + dx, sy + dy) == BLACK)
                        found = true;

            if(found)
                ++corners;
        }
    }
    CHECK(corners == 8);
}

void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if(!f)
    {
        printf("could not write %s\n", path);
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", W, H);

    for(int i = 0; i < W*H; ++i)
    {
        // COLOR is RGB565.
        const COLOR c = frame[i];
        const unsigned char rgb[3] = {
            (unsigned char)(((c >> 11) & 0b11111)*255/0b11111),
            (unsigned char)(((c >> 5) & 0b111111)*255/0b111111),
            (unsigned char)((c & 0b11111)*255/0b11111),
        };
        fwrite(rgb, 1, 3, f);
    }

    fclose(f);
    printf("wrote %s (%dx%d)\n", path, W, H);
}

}

extern "C"
{
    int SDL_Init(Uint32) { return 0; }
    int SDL_InitSubSystem(Uint32) { return 0; }
    SDL_Surface *SDL_SetVideoMode(int width, int height, int, Uint32)
    {
        static SDL_Surface surface;
        surface.w = width;
        surface.h = height;
        return &surface;
    }
    int SDL_LockSurface(SDL_Surface *) { return 0; }
    void SDL_UnlockSurface(SDL_Surface *) {}
    void SDL_UpdateRect(SDL_Surface *, Sint32, Sint32, Uint32, Uint32) {}
}

int main()
{
    static COLOR buffer[SCREEN_WIDTH*SCREEN_HEIGHT];
    frame = buffer;

    nglInit();
    nglSetBuffer(buffer, SCREEN_WIDTH, SCREEN_HEIGHT);
    glLoadIdentity();

    printf("nglines_test: screen %dx%d\n", W, H);

    testHorizontalLineIsOnePixel();
    testScreenVerticalLineDoesNotDivideByZero();
    testDiagonalLine();
    testPairsAreDisjoint();
    testDegenerateSegment();
    testVertexColourReachesTheFramebuffer();

    // Last, so the file it writes is the interesting one.
    drawWireframeBox();
    write_ppm("build/nglines_test.ppm");

    nglUninit();

    printf("nglines_test: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}