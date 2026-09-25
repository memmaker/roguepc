# Rogue PC, SDL2 port (RVIP 2026): make -C port
# The game sources in ../src are compiled unchanged except for the
# ROGUE_PORT hooks; src/curses.c is replaced by pcvideo.c.
SRC     := ../src
GAME    := armor new_leve command mach_dep rip save chase daemon daemons env \
	extern fakedos fight init io list maze misc monsters move pack \
	passages potions rings rooms scrolls sticks strings things weapons \
	wizard slime load protect main explore menu savegame
PORT    := pcvideo fe_sdl tiles
CC      ?= cc
CFLAGS  := -std=c17 -O2 -g -Wall -Wno-unused-parameter -Wno-unused-variable \
	-Wno-unused-but-set-variable -Wno-deprecated-non-prototype \
	-DMINROG -DROGUE_PORT -DROGUE_NO_X11 -DROGUE_CHARSET=2 \
	-I. -I$(SRC) $(shell pkg-config sdl2 --cflags) $(EXTRA)
LDLIBS  := $(shell pkg-config sdl2 --libs)
OBJ     := $(addprefix build/,$(addsuffix .o,$(GAME) $(PORT)))

roguepc: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: $(SRC)/%.c $(SRC)/*.h fe.h | build
	$(CC) $(CFLAGS) -c -o $@ $<
build/%.o: %.c fe.h tiles.h vgafont.h $(SRC)/*.h | build
	$(CC) $(CFLAGS) -c -o $@ $<
build:
	mkdir -p build

clean:
	rm -rf build roguepc roguepc.dSYM
.PHONY: clean
