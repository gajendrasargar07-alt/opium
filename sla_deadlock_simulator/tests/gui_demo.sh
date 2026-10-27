#!/bin/sh
# gui_demo.sh - starts the GUI on a virtual display, drives it with real X events (xdrive)
# and saves screenshots to ./screens/.  Needs: xvfb, imagemagick (import), libx11-dev.
#   sudo apt install xvfb imagemagick
cd "$(dirname "$0")/.." || exit 1
make deadlock_gui xdrive >/dev/null || exit 1
mkdir -p screens
cat > /tmp/sla_demo_inner.sh <<'INNER'
./deadlock_gui 3 & GUI=$!
sleep 1
./xdrive <<'XD'
click 380 157
sleep 400
XD
import -window root screens/01_deadlock_prone_both.png
./xdrive <<'XD'
click 220 54
click 220 78
click 380 157
sleep 400
XD
import -window root screens/02_textbook_both.png
./xdrive <<'XD'
click 90 107
key a ctrl
type 3 x 2
key F5
sleep 300
XD
import -window root screens/03_error_dialog.png
kill $GUI
INNER
xvfb-run -a -s "-screen 0 1120x640x24" sh /tmp/sla_demo_inner.sh
ls screens
