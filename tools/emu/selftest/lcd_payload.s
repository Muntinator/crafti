@ Bare-metal ARM payload used by tools/emu/selftest.sh.
@
@ It runs with no OS and no stack: it paints the calculator's LCD framebuffer
@ with an asymmetric shape, points the LCD controller at it and turns the panel
@ on. The shape is a white band along the top plus a white column down the left
@ edge, on a black background - an "L" that no flip or rotation can hide.
@
@ Only black (0x0000) and white (0xFFFF) are used on purpose: these are the two
@ values that mean the same thing in every 16bpp layout a CX panel may use
@ (Firebird reads CX framebuffers as BGR565 unless control bit 8 selects RGB),
@ so the test asserts geometry without assuming a pixel format.
@
@ CX LCD controller: 0xC0000000, UPBASE +0x10, CONTROL +0x18
@   CONTROL = enable(1) | 16bpp mode(4<<1) | TFT(1<<5) | LCD power(1<<11)

        .arm
        .global _start

_start:
        ldr     r0, =0x11000000         @ framebuffer, 320x240x16bpp
        mov     r4, #0                  @ y

yloop:
        mov     r3, #0                  @ x
xloop:
        mov     r1, #0                  @ black
        cmp     r4, #40                 @ top band?
        blt     white
        cmp     r3, #40                 @ left column?
        blt     white
        b       store
white:
        mvn     r1, #0                  @ 0xFFFF
store:
        strh    r1, [r0], #2

        add     r3, r3, #1
        cmp     r3, #320
        blt     xloop

        add     r4, r4, #1
        cmp     r4, #240
        blt     yloop

        ldr     r0, =0xC0000000         @ LCD controller
        ldr     r1, =0x11000000
        str     r1, [r0, #0x10]         @ UPBASE
        ldr     r1, =0x829              @ enable | 16bpp | TFT | power
        str     r1, [r0, #0x18]         @ CONTROL

hang:
        b       hang

        .ltorg
