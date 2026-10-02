#!/bin/sh
# Read a calculator screenshot without a screen to look at.
#
#   sh tools/emu/read-screen.sh shot.png            whole screen
#   sh tools/emu/read-screen.sh shot.png 180 50 140 120    (x y w h) crop
#
# The CX panel is a dark-on-light LCD, so the picture is upscaled and negated
# before OCR: tesseract reads that far more reliably than the raw capture, and a
# crop of one panel avoids mixing a menu with the text behind it.
#
# Needs ffmpeg and tesseract (both are in the workspace image).

set -e

shot=$1
if [ -z "$shot" ]; then
    echo "usage: $0 <screenshot.png> [x y w h]" >&2
    exit 2
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

if [ $# -ge 5 ]; then
    filter="crop=$4:$5:$2:$3"
    scale=8
else
    filter="crop=iw:ih:0:0"
    scale=6
fi

ffmpeg -loglevel error -y -i "$shot" \
    -vf "$filter,scale=iw*$scale:ih*$scale:flags=neighbor,format=gray,negate" \
    "$tmp/crop.png"

tesseract "$tmp/crop.png" - --psm 6 2>/dev/null | grep -v '^[[:space:]]*$'
