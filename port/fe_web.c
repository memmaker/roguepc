/*
 * fe_web.c - browser frontend of the Rogue PC port (RVIP step 7)
 *
 * Replaces fe_sdl.c in the WebAssembly build. C only describes the frame;
 * web/roguepc.js draws it (Module.rp):
 *  - text mode: one window, the 80x25 IBM text screen (VGA font, CGA colours)
 *  - tiles mode: the tiling windows of the other web ports (map, messages,
 *    status, inventory, pop-up over the map), with the page's top bar.
 * Input waits with Asyncify (emscripten_sleep).
 */

#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rogue.h"
#include "fe.h"
#include "vgafont.h"
#include "tiles.h"

int fe_click_row, fe_click_col;
int fe_ingame = 0;
int fe_auto_more = 0;
int fe_msgs = 0;

static int npop = 0, pops[4][4];

/* ---- message history ---------------------------------------------------- */
EM_JS(void, js_msg, (const char *s), { Module.rp.msg(UTF8ToString(s)); });

void
fe_msg(const char *msg)
{
	char line[81];
	int len = strlen(msg), n;

	while (len > 0)
	{
		n = len > 79 ? 79 : len;
		if (n < len)
			while (n > 20 && msg[n] != ' ')
				n--;
		snprintf(line, sizeof line, "%.*s", n, msg);
		js_msg(line);
		msg += n;
		len -= n;
		while (*msg == ' ')
			msg++, len--;
	}
	fe_msgs++;
}

/* ---- settings (auto_more; the page keeps its own layout) ---------------- */
EM_JS(void, js_sync, (void), { Module.rp.sync(); });

void
fe_save_cfg(void)
{
	FILE *f = fopen("roguepc.cfg", "w");

	if (f)
	{
		fprintf(f, "auto_more=%d\n", fe_auto_more);
		fclose(f);
		js_sync();
	}
}

EMSCRIPTEN_KEEPALIVE void
web_set_auto_more(int on)
{
	fe_auto_more = on;
	fe_save_cfg();
}

EM_JS(void, js_init, (const void *font, const void *tiles, const void *cols, int ntiles, int auto_more),
	{ Module.rp.init(font, tiles, cols, ntiles, auto_more); });

void
fe_init(void)
{
	static int inited;
	FILE *f;
	char buf[64];

	if (inited)
		return;
	inited = 1;
	if ((f = fopen("roguepc.cfg", "r")))
	{
		while (fgets(buf, sizeof buf, f))
			if (!strncmp(buf, "auto_more=", 10))
				fe_auto_more = buf[10] == '1';
		fclose(f);
	}
	js_init(vgafont, tile_bits, tile_col, NTILES, fe_auto_more);
}

/* ---- the frame ---------------------------------------------------------- */
static int map_t[22 * 80], map_u[22 * 80];
static char inv[40][81];
static unsigned char inv_at[40];
static unsigned char splash[320 * 200];
static int splash_on;

/* bounding box of what a full text screen (inventory, help) shows */
static int
screen_bbox(int *r0, int *c0, int *r1, int *c1)
{
	int r, c, any = 0;

	*r0 = 25; *c0 = 80; *r1 = -1; *c1 = -1;
	for (r = 0; r < 25; r++)
		for (c = 0; c < 80; c++)
		{
			unsigned short v = vram[r][c];
			if ((v & 0xff) != ' ' || (v >> 12 & 7))
			{
				if (r < *r0) *r0 = r;
				if (r > *r1) *r1 = r;
				if (c < *c0) *c0 = c;
				if (c > *c1) *c1 = c;
				any = 1;
			}
		}
	if (cur_on)
	{
		if (cur_row < *r0) *r0 = cur_row;
		if (cur_row > *r1) *r1 = cur_row;
		if (cur_col < *c0) *c0 = cur_col;
		if (cur_col + 1 > *c1) *c1 = cur_col + 1 > 79 ? 79 : cur_col + 1;
	}
	return any;
}

EM_JS(void, js_text, (const void *scr, int cr, int cc, int con, const void *pic),
	{ Module.rp.text(scr, cr, cc, con, pic); });
EM_JS(void, js_tiles, (const void *scr, const void *vr, const int *t, const int *u,
	const void *inv, const void *at, int ninv, int pr0, int pc0, int pr1, int pc1,
	int cr, int cc, int con, int hy, int hx, int lvl),
	{ Module.rp.tiles(scr, vr, t, u, inv, at, ninv, pr0, pc0, pr1, pc1, cr, cc, con, hy, hx, lvl); });

void
fe_present(void)
{
	unsigned short (*scr)[80] = is_saved ? saved_vram : vram;
	int r, c, i, r0 = -1, c0 = 0, r1 = -1, c1 = 0, n;

	fe_init();
	if (splash_on || curtain_down || !fe_ingame)
	{
		js_text(curtain_down ? curtain_vram : vram, cur_row, cur_col, cur_on,
			splash_on ? splash : NULL);
		return;
	}
	for (r = 1; r <= 22; r++)
		for (c = 0; c < 80; c++)
		{
			i = (r - 1) * 80 + c;
			map_t[i] = tile_for(r, c, scr[r][c] & 0xff, &map_u[i]);
		}
	n = inv_lines(inv, inv_at, 40);
	/* pop-up: the menus' boxes, or a whole text screen over the map */
	if (npop > 0)
	{
		r0 = 25; c0 = 80;
		for (i = 0; i < npop && i < 4; i++)
		{
			if (pops[i][0] < r0) r0 = pops[i][0];
			if (pops[i][1] < c0) c0 = pops[i][1];
			if (pops[i][2] > r1) r1 = pops[i][2];
			if (pops[i][3] > c1) c1 = pops[i][3];
		}
	}
	else if (!(is_saved && screen_bbox(&r0, &c0, &r1, &c1)))
		r0 = -1;
	js_tiles(scr, vram, map_t, map_u, inv, inv_at, n, r0, c0, r1, c1,
		cur_row, cur_col, cur_on, hero.y, hero.x, level);
}

void
fe_popup_push(int r0, int c0, int r1, int c1)
{
	if (npop < 4)
	{
		pops[npop][0] = r0;
		pops[npop][1] = c0;
		pops[npop][2] = r1;
		pops[npop][3] = c1;
	}
	npop++;
}

void
fe_popup_pop(void)
{
	if (npop > 0)
		npop--;
}

EM_JS(void, js_toggle, (void), { Module.rp.toggle(); });

void
fe_toggle_mode(void)
{
	js_toggle();
}

/* ---- CGA title picture: 2 bits per pixel, interlaced -------------------- */
void
fe_splash(const char *path)
{
	unsigned char data[7 + 16384];
	char alt[64];
	FILE *f;
	int y, x;

	splash_on = 0;
	if (path && !(f = fopen(path, "rb")))
	{
		snprintf(alt, sizeof alt, "/%s", path);	/* packaged with the game */
		f = fopen(alt, "rb");
	}
	if (!path || !f)
	{
		fe_present();
		return;
	}
	memset(data, 0, sizeof data);
	fread(data, 1, sizeof data, f);
	fclose(f);
	for (y = 0; y < 200; y++)
		for (x = 0; x < 320; x++)
		{
			unsigned char b = data[7 + (y & 1) * 8192 + (y / 2) * 80 + x / 4];
			splash[y * 320 + x] = (b >> (6 - 2 * (x & 3))) & 3;
		}
	splash_on = 1;
	fe_present();
}

/* ---- input -------------------------------------------------------------- */
EM_JS(int, js_key, (void), { return Module.rp.key(); });
EM_JS(int, js_click, (void), { return Module.rp.click(); });
EM_JS(void, js_flush_keys, (void), { Module.rp.flush(); });

int
fe_getkey(int msdelay)
{
	double start = emscripten_get_now();
	int k;

	fe_present();	/* the game relies on the wait to show the screen */
	for (;;)
	{
		if ((k = js_key()) >= 0)
		{
			if (k == FK_CLICK)
			{
				k = js_click();
				fe_click_row = k >> 8;
				fe_click_col = k & 0xff;
				return FK_CLICK;
			}
			return k;
		}
		if (msdelay >= 0 && emscripten_get_now() - start >= msdelay)
			return -1;
		emscripten_sleep(10);
	}
}

int
fe_kbhit(void)
{
	static double last;

	/* explore/running poll this: let the page paint now and then */
	if (emscripten_get_now() - last > 50)
	{
		last = emscripten_get_now();
		emscripten_sleep(0);
	}
	return EM_ASM_INT({ return Module.rp.pending(); });
}

void
fe_flush(void)
{
	js_flush_keys();
}

void
fe_beep(void)
{
}

void
fe_fatal(const char *msg)
{
	char line[81];
	int r = 24, c;

	while (*msg == '\n')
		msg++;
	snprintf(line, sizeof line, "%s", msg);
	for (c = 0; line[c]; c++)
		if (line[c] == '\n')
			line[c] = ' ';
	if (!*line)
		return;
	fe_ingame = 0;
	curtain_down = 0;
	is_saved = 0;
	for (c = 0; c < 80; c++)
		vram[r][c] = 0x0700 | ' ';
	for (c = 0; line[c] && c < 80; c++)
		vram[r][c] = 0x0f00 | (unsigned char)line[c];
	cur_on = 0;
	fe_present();
	while (fe_getkey(-1) == FK_CLICK)
		;
}
