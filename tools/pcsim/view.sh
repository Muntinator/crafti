#!/bin/sh
# Serve the frames the tour captured as a gallery.
#
#   sh tools/pcsim/view.sh [shots-dir]
#
# Defaults to $MUNT_SIM_SHOTS, or /tmp/munt-sim/shots. Nothing is built and
# nothing is captured here: run `sh tools/pcsim/play.sh` first, then open the
# page and reload it after each tour.

here=$(cd "$(dirname "$0")" && pwd)
exec python3 "$here/gallery.py" --shots "${1:-${MUNT_SIM_SHOTS:-/tmp/munt-sim/shots}}"
