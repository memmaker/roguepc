/*
 * pcvideo.c - replacement for src/curses.c (RVIP port, 2026)
 *
 * The game's own DOS "curses" API implemented on an in-memory copy of the
 * PC text screen: 25 x 80 cells of (CGA attribute << 8 | CP437 character),
 * exactly what the original wrote to video memory at B800:0000. The SDL
 * frontend (fe_sdl.c) draws that buffer either as the original IBM text mode
 * or with tiles. Game sources are unchanged apart from a few hooks.
 */

#include "extern.h"
#include "curses_common.h"
#include "fe.h"

int LINES = 25, COLS = 80;
int is_saved = FALSE;
int scr_type = -1;
char savewin[MAXLINES * MAXCOLS * 2];	/* 4000 bytes, as in the original */

unsigned short vram[MAXLINES][MAXCOLS];
unsigned short saved_vram[MAXLINES][MAXCOLS];	/* map under a text screen */
unsigned short curtain_vram[MAXLINES][MAXCOLS];
int curtain_down = FALSE;
int cur_row, cur_col, cur_on = FALSE;

#define A_BLACK		0x00
#define A_BLUE		0x01
#define A_GREEN		0x02
#define A_RED		0x04
#define A_WHITE		0x07
#define A_BRIGHT	0x08
#define A_BLINK		0x80
#define A_CYAN		(A_BLUE | A_GREEN)
#define A_MAGENTA	(A_BLUE | A_RED)
#define A_BROWN		(A_GREEN | A_RED)
#define A_YELLOW	(A_BROWN | A_BRIGHT)
#define A_BG(c)		(((c) & 7) << 4)
#define A_NORMAL	A_WHITE
#define A_STANDOUT	A_BG(A_WHITE)

static byte ch_attr = A_NORMAL;

#define MAXATTR 17
static byte color_attr[] = {
	A_NORMAL, A_GREEN, A_CYAN, A_RED, A_MAGENTA, A_BROWN,
	A_BRIGHT | A_BLACK, A_BRIGHT | A_BLUE, A_BRIGHT | A_GREEN,
	A_BRIGHT | A_RED, A_BRIGHT | A_MAGENTA, A_BRIGHT | A_BROWN,
	A_BRIGHT | A_WHITE, A_BLUE, A_STANDOUT, A_BRIGHT | A_NORMAL,
	A_STANDOUT, 0
};
static byte monoc_attr[] = {
	A_NORMAL, A_NORMAL, A_NORMAL, A_NORMAL, A_NORMAL, A_NORMAL, A_NORMAL,
	A_NORMAL, A_NORMAL, A_NORMAL, A_NORMAL, A_NORMAL, 1 | A_BG(1),
	A_NORMAL, A_STANDOUT | A_BRIGHT, A_NORMAL, A_STANDOUT | A_BRIGHT, 0
};
static byte *at_table = color_attr;

static byte dbl_box[7] = { DULCORNER, DURCORNER, DLLCORNER, DLRCORNER, DVLINE, DHLINE, DHLINE };
static byte sng_box[7] = { ULCORNER, URCORNER, LLCORNER, LRCORNER, VLINE, HLINE, HLINE };
static byte spc_box[7] = { ' ', ' ', ' ', ' ', ' ', ' ', ' ' };

/* IBM extended keys -> commands, as in the DOS original (keypad = NumLock off) */
static struct { int key; byte is; } xtab[] = {
	{FK_HOME, 'y'}, {FK_UP, 'k'}, {FK_PGUP, 'u'}, {FK_LEFT, 'h'},
	{FK_RIGHT, 'l'}, {FK_END, 'b'}, {FK_DOWN, 'j'}, {FK_PGDN, 'n'},
	{FK_INS, '>'}, {FK_DEL, 's'},
	{FK_KP7, 'y'}, {FK_KP8, 'k'}, {FK_KP9, 'u'}, {FK_KP4, 'h'},
	{FK_KP6, 'l'}, {FK_KP1, 'b'}, {FK_KP2, 'j'}, {FK_KP3, 'n'},
	{FK_KP5, '.'}, {FK_KP0, '>'}, {FK_KPDOT, 's'}, {FK_KPENTER, '\n'},
	{FK_KPPLUS, '+'}, {FK_KPMINUS, '-'}, {FK_KPSTAR, '*'},
	{FK_F1, '?'}, {FK_F2, '/'}, {FK_F3, 'a'}, {FK_F4, CTRL('R')},
	{FK_F5, 'c'}, {FK_F6, 'D'}, {FK_F7, 'i'}, {FK_F8, '^'},
	{FK_F9, CTRL('F')}, {FK_F10, '!'}, {FK_ALTF9, 'F'},
};

byte
xlate_ch(int ch)
{
	unsigned i;

	for (i = 0; i < sizeof xtab / sizeof *xtab; i++)
		if (ch == xtab[i].key)
			return xtab[i].is;
	return ch >= 0x100 ? 0 : (byte)ch;
}

void
cur_beep(void)
{
	fe_beep();
}

int
cur_getch_timeout(int msdelay)
{
	return fe_getkey(msdelay);
}

int
cur_move(int row, int col)
{
	if (row < 0 || row >= LINES || col < 0 || col >= COLS)
		return -1;
	cur_row = row;
	cur_col = col;
	return 0;
}

byte
cur_inch(void)
{
	return vram[cur_row][cur_col] & 0xff;
}

static void
putchr(byte ch)
{
	if (cur_row < LINES && cur_col < COLS)
		vram[cur_row][cur_col] = (ch_attr << 8) | ch;
}

void
cur_clear(void)
{
	int r, c;

	for (r = 0; r < LINES; r++)
		for (c = 0; c < COLS; c++)
			vram[r][c] = (A_NORMAL << 8) | ' ';
	cur_row = cur_col = 0;
}

bool
cursor(bool ison)
{
	bool old = cur_on;

	cur_on = ison;
	return old;
}

void
getrc(int *rp, int *cp)
{
	*rp = cur_row;
	*cp = cur_col;
}

void
cur_refresh(void)
{
	fe_present();
}

void
cur_clrtoeol(void)
{
	int c;

	for (c = cur_col; c < COLS; c++)
		vram[cur_row][c] = (A_NORMAL << 8) | ' ';
}

void
cur_mvaddstr(int r, int c, char *s)
{
	cur_move(r, c);
	cur_addstr(s);
}

void
cur_mvaddch(int r, int c, byte chr)
{
	cur_move(r, c);
	cur_addch(chr);
}

byte
cur_mvinch(int r, int c)
{
	cur_move(r, c);
	return cur_inch();
}

/*
 * The original colour rules for dungeon characters (see src/curses.c)
 */
void
cur_addch(byte chr)
{
	byte old_attr = ch_attr;

	if (at_table == color_attr)
	{
		if (ch_attr == A_NORMAL)
		{
			switch (chr)
			{
			case DOOR: case VWALL: case HWALL: case ULWALL:
			case URWALL: case LLWALL: case LRWALL:
				ch_attr = A_BROWN;
				break;
			case FLOOR:
				ch_attr = A_GREEN | A_BRIGHT;
				break;
			case STAIRS:
				ch_attr = A_BLACK | A_BG(A_GREEN) | A_BLINK;
				break;
			case TRAP:
				ch_attr = A_MAGENTA;
				break;
			case GOLD: case PLAYER:
				ch_attr = A_YELLOW;
				break;
			case POTION: case SCROLL: case STICK: case ARMOR:
			case AMULET: case RING: case WEAPON:
				ch_attr = A_BLUE | A_BRIGHT;
				break;
			case FOOD:
				ch_attr = A_RED;
				break;
			}
		}
		else if (ch_attr == A_STANDOUT)
		{
			switch (chr)
			{
			case FOOD:
				ch_attr = A_RED | A_STANDOUT;
				break;
			case GOLD: case PLAYER:
				ch_attr = A_YELLOW | A_STANDOUT;
				break;
			case POTION: case SCROLL: case STICK: case ARMOR:
			case AMULET: case RING: case WEAPON:
				ch_attr = A_BLUE | A_STANDOUT;
				break;
			}
		}
		else if (ch_attr == (A_BRIGHT | A_NORMAL) && chr == STAIRS)
			ch_attr = A_BLACK | A_BG(A_GREEN) | A_BLINK;
	}
	if (chr == '\n')
	{
		if (cur_row == LINES - 1)
		{
			memmove(vram[0], vram[1], sizeof vram[0] * (LINES - 1));
			for (int c = 0; c < COLS; c++)
				vram[LINES - 1][c] = (A_NORMAL << 8) | ' ';
		}
		else
			cur_row++;
		cur_col = 0;
	}
	else
	{
		putchr(chr);
		if (cur_col < COLS - 1)
			cur_col++;
		else if (cur_row < LINES - 1)
		{
			/* wrap like the BIOS teletype does (add_line() relies on it) */
			cur_col = 0;
			cur_row++;
		}
	}
	ch_attr = old_attr;
}

void
cur_addstr(char *s)
{
	while (*s)
		cur_addch(*s++);
}

void
set_attr(int bute)
{
	ch_attr = bute < MAXATTR ? at_table[bute] : bute;
}

void
winit(void)
{
	LINES = 25;
	COLS = 80;
	scr_type = 3;		/* 80x25 colour */
	at_table = bwflag ? monoc_attr : color_attr;
	fe_init();
}

/*
 * wdump / wrestor: save the screen while a text screen (inventory, help,
 * discoveries, ...) covers it. savewin is also what save files store.
 */
void
wdump(void)
{
	int r, c;

	for (r = 0; r < LINES; r++)
		for (c = 0; c < COLS; c++)
		{
			savewin[(r * COLS + c) * 2] = vram[r][c] & 0xff;
			savewin[(r * COLS + c) * 2 + 1] = vram[r][c] >> 8;
		}
	memcpy(saved_vram, vram, sizeof vram);
	is_saved = TRUE;
}

void
wrestor(void)
{
	int r, c;

	for (r = 0; r < LINES; r++)
		for (c = 0; c < COLS; c++)
			vram[r][c] = (byte)savewin[(r * COLS + c) * 2]
				| (byte)savewin[(r * COLS + c) * 2 + 1] << 8;
	is_saved = FALSE;
	fe_present();
}

void
cur_endwin(void)
{
}

static void
repchr_raw(byte chr, int cnt)
{
	while (cnt-- > 0 && cur_col < COLS)
		vram[cur_row][cur_col++] = (ch_attr << 8) | chr;
	if (cur_col >= COLS)
		cur_col = COLS - 1;
}

void
repchr(byte chr, int cnt)
{
	repchr_raw(chr, cnt);
}

static void
vbox(byte box[7], int ul_r, int ul_c, int lr_r, int lr_c)
{
	int r = cur_row, c = cur_col, i;

	cur_move(ul_r, ul_c + 1); repchr_raw(box[5], lr_c - ul_c - 1);
	cur_move(lr_r, ul_c + 1); repchr_raw(box[6], lr_c - ul_c - 1);
	for (i = ul_r + 1; i < lr_r; i++)
	{
		vram[i][ul_c] = (ch_attr << 8) | box[4];
		vram[i][lr_c] = (ch_attr << 8) | box[4];
	}
	vram[ul_r][ul_c] = (ch_attr << 8) | box[0];
	vram[ul_r][lr_c] = (ch_attr << 8) | box[1];
	vram[lr_r][ul_c] = (ch_attr << 8) | box[2];
	vram[lr_r][lr_c] = (ch_attr << 8) | box[3];
	cur_row = r;
	cur_col = c;
}

void
cur_box(int ul_r, int ul_c, int lr_r, int lr_c)
{
	vbox(dbl_box, ul_r, ul_c, lr_r, lr_c);
}

void
center(int row, char *string)
{
	cur_mvaddstr(row, (COLS - strlen(string)) / 2, string);
}

void
cur_printw(const char *msg, ...)
{
	char pwbuf[132];
	va_list argp;

	va_start(argp, msg);
	vsnprintf(pwbuf, sizeof pwbuf, msg, argp);
	va_end(argp);
	cur_addstr(pwbuf);
}

/*
 * Clear the screen in an interesting fashion
 */
void
implode(void)
{
	int j, r, c, cinc = COLS / 10 / 2, er, ec;

	er = LINES - 3;
	for (r = 0, c = 0, ec = COLS - 1; r < 10; r++, c += cinc, er--, ec -= cinc)
	{
		vbox(sng_box, r, c, er, ec);
		fe_present();
		msleep(25);
		for (j = r + 1; j <= er - 1; j++)
		{
			cur_move(j, c + 1); repchr_raw(' ', cinc - 1);
			cur_move(j, ec - cinc + 1); repchr_raw(' ', cinc - 1);
		}
		vbox(spc_box, r, c, er, ec);
	}
	fe_present();
}

/*
 * Close a curtain over the screen; the game draws the new level behind it
 */
void
drop_curtain(void)
{
	int r;
	int delay = CURTAIN_TIME / LINES;

	cursor(FALSE);
	green();
	vbox(sng_box, 0, 0, LINES - 1, COLS - 1);
	fe_present();
	yellow();
	for (r = 1; r < LINES - 1; r++)
	{
		cur_move(r, 1);
		repchr_raw(FILLER, COLS - 2);
		fe_present();
		msleep(delay);
	}
	memcpy(curtain_vram, vram, sizeof vram);
	curtain_down = TRUE;
	cur_move(0, 0);
	cur_standend();
	cur_clear();
}

void
raise_curtain(void)
{
	int r;
	int delay = CURTAIN_TIME / LINES;

	for (r = LINES - 1; r >= 0; r--)
	{
		memcpy(curtain_vram[r], vram[r], sizeof vram[r]);
		fe_present();
		msleep(delay);
	}
	curtain_down = FALSE;
	is_saved = FALSE;
	fe_present();
}

byte
get_mode(void)
{
	return 3;
}

byte
video_mode(int type)
{
	fe_splash(type == 4 ? "rogue.pic" : NULL);	/* CGA title picture */
	return type;
}

/*
 * Read a line from the keyboard (see src/curses.c for the original notes)
 */
int
getinfo(char *str, int size)
{
	char *retstr = str;
	int ch, readcnt = 0, wason, ret = 1;

	*str = 0;
	wason = cursor(TRUE);
	while (ret == 1)
	{
		ch = fe_getkey(-1);
		if (ch == FK_KPENTER)
			ch = '\n';
		switch (ch)
		{
		case ESCAPE:
			while (str != retstr)
			{
				backspace();
				readcnt--;
				str--;
			}
			ret = *str++ = ESCAPE;
			*str = 0;
			cursor(wason);
			break;
		case '\b':
			if (str != retstr)
			{
				backspace();
				readcnt--;
				str--;
			}
			break;
		case '\n':
			*str = 0;
			cursor(wason);
			ret = ch;
			break;
		default:
			if (ch < 0 || ch > 0xff || !isprint(ch))
				break;
			if (readcnt >= size)
			{
				cur_beep();
				break;
			}
			readcnt++;
			cur_addch(ch);
			*str++ = ch;
			break;
		}
		fe_present();
	}
	return ret;
}

void
backspace(void)
{
	if (cur_col > 0)
		cur_col--;
	vram[cur_row][cur_col] = (A_NORMAL << 8) | ' ';
}
