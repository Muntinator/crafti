#!/bin/sh
# Self-test for the headless Firebird setup built by tools/emu/setup_firebird.sh.
#
# Runs a tiny bare-metal ARM payload in the emulator that paints the CX LCD in
# four colours, then checks the captured PNG: every quadrant must come out as
# the colour the payload wrote. This exercises the emulator core, the scripted
# screenshot path and the PNG writer without needing any calculator dump.
#
#   sh tools/emu/selftest.sh
#
# Environment:
#   EMU   path to munt-headless (default: $HOME/firebird/munt-headless)

set -eu

EMU=${EMU:-$HOME/firebird/munt-headless}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=${TMPDIR:-/tmp}/munt-emu-selftest

if [ ! -x "$EMU" ]; then
    echo "munt-headless not found at $EMU"
    echo "Build it first: sh tools/emu/setup_firebird.sh"
    exit 1
fi

for tool in arm-none-eabi-as arm-none-eabi-ld arm-none-eabi-objcopy ffmpeg; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool"; exit 1; }
done

rm -rf "$WORK"
mkdir -p "$WORK/shots"

# The payload is what actually executes, so boot1 only has to be a valid-sized
# image; make it a single branch-to-self instruction.
printf '\xFE\xFF\xFF\xEA' > "$WORK/boot1.img"   # b .
dd if=/dev/zero bs=1024 count=511 >> "$WORK/boot1.img" 2>/dev/null

echo "== creating a blank CX flash image"
"$EMU" --model cx --create-flash "$WORK/cx.img"
"$EMU" --info "$WORK/cx.img"

echo "== assembling the payload"
arm-none-eabi-as -o "$WORK/payload.o" "$HERE/selftest/lcd_payload.s"
arm-none-eabi-ld -Ttext=0x10000000 -o "$WORK/payload.elf" "$WORK/payload.o"
arm-none-eabi-objcopy -O binary "$WORK/payload.elf" "$WORK/payload.bin"

echo "== running the emulator"
"$EMU" --boot1 "$WORK/boot1.img" --flash "$WORK/cx.img" \
       --rampayload "$WORK/payload.bin" \
       --run 1 --shot-dir "$WORK/shots" --shot-at 0.5 --quiet

SHOT="$WORK/shots/final.png"
[ -f "$SHOT" ] || { echo "FAIL: no screenshot was written"; exit 1; }

echo "== checking the captured pixels"
ffmpeg -v error -i "$SHOT" -f rawvideo -pix_fmt rgb24 "$WORK/shot.rgb"

pixel() { # x y -> "r g b"
    dd if="$WORK/shot.rgb" bs=1 skip=$(( ( $2 * 320 + $1 ) * 3 )) count=3 2>/dev/null \
        | od -An -tu1 | awk '{ print $1, $2, $3 }'
}

check() { # x y expected-rgb label
    got=$(pixel "$1" "$2")
    if [ "$got" = "$3" ]; then
        echo "  ok   $4 -> $got"
    else
        echo "  FAIL $4: expected $3, got $got"
        FAILED=1
    fi
}

# The payload drew a white band along the top (y < 40) and a white column down
# the left (x < 40) on black. Any flip or rotation moves one of these.
FAILED=0
check 10 10 "255 255 255" "inside the top band"
check 300 10 "255 255 255" "top band reaches the right edge"
check 10 200 "255 255 255" "inside the left column"
check 39 239 "255 255 255" "left column reaches the bottom edge"
check 60 200 "0 0 0" "right of the left column"
check 300 200 "0 0 0" "bottom-right is empty"
check 150 60 "0 0 0" "below the top band"

if [ "$FAILED" = 0 ]; then
    echo
    echo "PASS: emulator + screenshots verified ($SHOT)"
else
    echo
    echo "FAIL: see $WORK"
    exit 1
fi
