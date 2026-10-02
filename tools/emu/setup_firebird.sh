#!/bin/sh
# Build the Firebird TI-Nspire emulator (GPLv3) plus the Muntcraft headless
# frontend that captures scripted CX screenshots.
#
#   sh tools/emu/setup_firebird.sh
#
# Environment:
#   FIREBIRD_DIR   where to clone and build (default: $HOME/firebird)
#   FIREBIRD_REF   branch or tag to check out (default: master)
#
# Results:
#   $FIREBIRD_DIR/munt-headless        Muntcraft's scriptable frontend
#   $FIREBIRD_DIR/headless/firebird-headless   upstream's benchmark frontend
#
# Nothing in this script touches the calculator build: crafti.tns is unaffected.

set -eu

FIREBIRD_DIR=${FIREBIRD_DIR:-$HOME/firebird}
FIREBIRD_REF=${FIREBIRD_REF:-master}
REPO=https://github.com/nspire-emus/firebird.git
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if [ ! -d "$FIREBIRD_DIR/.git" ]; then
    echo "== cloning Firebird into $FIREBIRD_DIR"
    git clone --depth 1 --branch "$FIREBIRD_REF" "$REPO" "$FIREBIRD_DIR"
fi

cd "$FIREBIRD_DIR"

echo "== fetching the gif-h submodule"
git submodule update --init --depth 1

echo "== building the emulation core"
make -C headless -j"$(nproc)"

echo "== building munt-headless"
# Reuses the core objects the headless build just produced.
g++ -std=c++11 -O3 -DSUPPORT_LINUX -I"$FIREBIRD_DIR" \
    "$HERE/munt-headless.cpp" \
    "$FIREBIRD_DIR"/core/*.o "$FIREBIRD_DIR"/core/os/*.o \
    -lz -o "$FIREBIRD_DIR/munt-headless"

echo
echo "== done: $FIREBIRD_DIR/munt-headless"
"$FIREBIRD_DIR/munt-headless" --help | sed -n '2,6p'
