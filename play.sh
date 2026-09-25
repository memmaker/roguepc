#!/bin/sh
# Rogue PC (Epyx 1985), SDL2 port (port/): one window, Tiles/Text buttons
# (F12). Text = original IBM CP437 font and colours. Saves/scores in save/.
cd "$(dirname "$0")/save" || exit 1
if [ -f rogue.sav ]; then exec ../port/roguepc -r "$@"; fi
exec ../port/roguepc "$@"
