#!/bin/sh
# Build Rogue PC for the browser (Emscripten + Asyncify) into web/dist;
# port/fe_web.c + web/roguepc.js draw the windows (no SDL). Deploy with web/deploy.sh.
set -e
cd "$(dirname "$0")/../port"
OUT=../web/dist
rm -rf "$OUT" && mkdir -p "$OUT"
GAME=$(sed -n '/^GAME/,/^PORT/p' Makefile | sed 's/GAME *:=//;/^PORT/d' | tr -d '\\')
SRCS=$(for f in $GAME; do printf '../src/%s.c ' $f; done)
emcc -O2 -std=gnu17 -w -DMINROG -DROGUE_PORT -DROGUE_NO_X11 -DROGUE_CHARSET=2 -I. -I../src \
	$SRCS pcvideo.c fe_web.c tiles.c --embed-file ../data/rogue.pic@/rogue.pic -o "$OUT/roguepc-core.js" \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sSTACK_SIZE=1048576 \
	-sALLOW_MEMORY_GROWTH -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAPU8,HEAPU16,HEAP32,HEAPU32,UTF8ToString,addRunDependency,removeRunDependency \
	-sEXPORTED_FUNCTIONS=_main,_web_set_auto_more \
	-sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web -sEXIT_RUNTIME=0
cp ../web/index.html ../web/roguepc.js "$HOME/Games/rvip-tools/web/rvip-wm.js" tiles-dawn.png "$OUT/"
python3 ../web/make-help.py > "$OUT/help.html"
ls -la "$OUT"
