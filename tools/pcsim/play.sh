#!/bin/sh
# Build the headless desktop harness and play the scripted tour.
#
#   sh tools/pcsim/play.sh [scenario]
#
# Frames land in $MUNT_SIM_SHOTS (default /tmp/munt-sim/shots), listed by
# manifest.txt. `sh tools/pcsim/view.sh` then serves them as a gallery.
#
# Nothing here changes the game or its builds: it compiles the same sources the
# desktop Makefile.pc does, against the fake SDL in tools/pcsim/SDL.

set -e

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cd "$root"

scenario=${1:-tools/pcsim/tour.txt}
out=${MUNT_SIM_SHOTS:-/tmp/munt-sim/shots}
world=${MUNT_SIM_WORLD:-/tmp/munt-sim/world.tns}

echo "== building the harness"
make -C tools/pcsim -s

# A fresh save file every time: otherwise the title screen offers to continue a
# world this run did not create, and the tour starts somewhere else.
rm -rf "$out"
mkdir -p "$(dirname "$out")"
rm -f "$world"

echo "== playing $scenario"
MUNT_SIM_SCRIPT="$scenario" \
MUNT_SIM_SHOTS="$out" \
    ./tools/pcsim/build/crafti-sim "$world"

echo "== frames in $out"
ls "$out" | wc -l
