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
#include "curses.h"
#include "fe.h"
#include "vgafont.h"
#include "tiles.h"

/* Run report (roguelikes-index/server/CONTRACT.md): fire-and-forget GET,
   never throws, offline just fails silently. Negative ints are omitted. */
EM_JS(void, js_beacon, (const char *g, const char *ev, const char *name, const char *killer, int depth, int score, int turns, int lvl), {
    try {
        var p = [['g', UTF8ToString(g)], ['ev', UTF8ToString(ev)], ['name', name ? UTF8ToString(name) : ''],
                 ['killer', killer ? UTF8ToString(killer) : ''], ['depth', depth], ['score', score], ['turns', turns], ['lvl', lvl]];
        var q = p.filter(function (a) { return a[1] !== '' && !(a[1] < 0); })
                 .map(function (a) { return a[0] + '=' + encodeURIComponent(a[1]); }).join('&');
        if (window.RvipWM && RvipWM.report) RvipWM.report(q); else fetch('/roguelikes/beacon?' + q, { keepalive: true, mode: 'no-cors' }).catch(function () {});
    } catch (e) {}
});
void fe_run_end(const char *ev, const char *killer, int score)
{
    js_beacon("roguepc", ev, whoami, killer, level, score, -1, pstats.s_lvl);
}

int fe_click_row, fe_click_col;
int fe_ingame = 0;
int fe_auto_more = 1;
int fe_msgs = 0;

static int npop = 0, pops[4][4];

/* ---- text windows (RVIP W0 rule 6): HTML lines, sent by row ------------
 * Status, Messages, Inventory and the pop-up are HTML text on the page. C
 * sends each changed row once, trimmed (no trailing spaces), as UTF-8:
 * standout (reverse) between \x01 and \x02, another colour than the row's
 * between "\x05#rrggbb" and \x06; per row its CSS colour and icon tile
 * (-1: none); and the rows in use (last non-blank or the cursor's row). */
enum { P_STAT, P_MSG, P_INV, P_POP, NPANE };
#define HIST 400
#define MAXROW (HIST + 1)
#define LW 1024
struct row { char s[LW]; char css[8]; int tile, dirty; };
static struct row rows_[NPANE][MAXROW];
static int nrows[NPANE], sent_rows[NPANE] = { -1, -1, -1, -1 };
static const char *pal[16] = { "#000000", "#0000aa", "#00aa00", "#00aaaa", "#aa0000", "#aa00aa", "#aa5500", "#aaaaaa",
	"#555555", "#5555ff", "#55ff55", "#55ffff", "#ff5555", "#ff55ff", "#ffff55", "#ffffff" };
static const unsigned short cp437[256] = {
	0x0020, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022, 0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
	0x25ba, 0x25c4, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8, 0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc,
	0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027, 0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
	0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
	0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047, 0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
	0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
	0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
	0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077, 0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x2302,
	0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, 0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
	0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, 0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
	0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, 0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, 0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, 0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
	0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, 0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
	0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, 0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x0020,
};

EM_JS(void, be_line, (int p, int y, const char *s, const char *css, int tile), { Module.rp.line(p, y, UTF8ToString(s), UTF8ToString(css), tile); });
EM_JS(void, be_rows, (int p, int n), { Module.rp.rows(p, n); });
EM_JS(void, be_cursor, (int p, int y, int x), { Module.rp.cursor(p, y, x); });
EM_JS(void, be_popup, (int on, int row, int col), { Module.rp.popup(on, row, col); });
EM_JS(void, be_prompt, (const char *s), { RvipWM.prompt.text(UTF8ToString(s)); });

static void
wc_line(int p, int y, const char *s)
{
	struct row *r = &rows_[p][y];

	if (strcmp(r->s, s))
	{
		snprintf(r->s, LW, "%s", s);
		r->dirty = 1;
	}
}

/* a row's colour and icon; marks the row changed */
static void
wc_rowattr(int p, int y, const char *css, int tile)
{
	struct row *r = &rows_[p][y];

	if (strcmp(r->css, css) || r->tile != tile)
	{
		snprintf(r->css, sizeof r->css, "%s", css);
		r->tile = tile;
		r->dirty = 1;
	}
}

static int cur_p = -1, cur_y, cur_x;
static void
wc_cursor(int p, int y, int x)
{
	if (p != cur_p || y != cur_y || x != cur_x)
	{
		cur_p = p; cur_y = y; cur_x = x;
		be_cursor(p, y, x);
	}
}

/* send the changed rows of pane p, which uses n rows */
static void
wc_flush(int p, int n)
{
	int y;

	if (cur_p == p && cur_y >= n)
		n = cur_y + 1;
	nrows[p] = n;
	for (y = 0; y < n; y++)
		if (rows_[p][y].dirty)
		{
			rows_[p][y].dirty = 0;
			be_line(p, y, rows_[p][y].s, rows_[p][y].css, rows_[p][y].tile);
		}
	if (sent_rows[p] != n)
		be_rows(p, sent_rows[p] = n);
}

/* forget what the page shows of pane p (it was cleared) */
static void
wc_reset(int p)
{
	int y;

	for (y = 0; y < MAXROW; y++)
	{
		rows_[p][y].s[0] = 0;
		rows_[p][y].css[0] = 0;
		rows_[p][y].tile = -1;
		rows_[p][y].dirty = 1;
	}
	sent_rows[p] = -1;
}

static char *
put_utf8(char *o, unsigned c)
{
	if (c < 0x80)
		*o++ = c;
	else if (c < 0x800)
	{
		*o++ = 0xc0 | c >> 6;
		*o++ = 0x80 | (c & 0x3f);
	}
	else
	{
		*o++ = 0xe0 | c >> 12;
		*o++ = 0x80 | (c >> 6 & 0x3f);
		*o++ = 0x80 | (c & 0x3f);
	}
	return o;
}

/* n text cells (attr << 8 | char) as a trimmed line; *fg: the row's colour
   (of its first non-blank cell) */
static void
enc_cells(char *out, const unsigned short *v, int n, int *fg)
{
	char *o = out;
	int i, end = n, so = 0, run = -1;

	while (end > 0 && (v[end - 1] & 0xff) <= ' ' && !(v[end - 1] >> 12 & 7))
		end--;
	*fg = 7;
	for (i = 0; i < end; i++)
		if ((v[i] & 0xff) > ' ' && !(v[i] >> 12 & 7))
		{
			*fg = v[i] >> 8 & 15;
			break;
		}
	for (i = 0; i < end; i++)
	{
		int a = v[i] >> 8, g = v[i] & 0xff, bg = a >> 4 & 7, c = a & 15;
		if (bg && !so)
		{
			if (run >= 0) *o++ = 6, run = -1;
			*o++ = 1, so = 1;
		}
		else if (!bg && so)
			*o++ = 2, so = 0;
		if (!bg && g > ' ' && c != *fg && c != run)
		{
			if (run >= 0) *o++ = 6;
			o += sprintf(o, "\x05%s", pal[c]);
			run = c;
		}
		else if (!bg && g > ' ' && c == *fg && run >= 0)
			*o++ = 6, run = -1;
		o = put_utf8(o, g ? cp437[g] : ' ');
	}
	if (run >= 0) *o++ = 6;
	if (so) *o++ = 2;
	*o = 0;
}

/* a plain game string (CP437) as UTF-8 */
static void
enc_str(char *out, const char *s)
{
	char *o = out;

	while (*s)
		o = put_utf8(o, cp437[(unsigned char)*s++]);
	while (o > out && o[-1] == ' ')
		o--;
	*o = 0;
}

/* ---- message history ---------------------------------------------------- */
static char hist[HIST][81];
static int nhist;

static void
hist_add(const char *s, int fold)
{
	if (fold && nhist)
	{
		snprintf(hist[nhist - 1], 81, "%s", s);
		return;
	}
	if (nhist == HIST)
	{
		memmove(hist[0], hist[1], sizeof hist[0] * (HIST - 1));
		nhist--;
	}
	snprintf(hist[nhist++], 81, "%s", s);
}

void
fe_msg(const char *msg)
{
	static char prev[81];
	static int reps;
	char line[81];
	int len = strlen(msg), n;

	/* a repeat of the last one-line message becomes "message (xN)",
	   replacing the last line (fold) */
	/* ponytail: wrapped (over 79 characters) messages don't fold */
	if (*prev && !strcmp(msg, prev)) {
		n = snprintf(line, sizeof line, "%s (x%d)", msg, ++reps);
		if (n < 80) {
			hist_add(line, 1);
			return;
		}
	}
	reps = 1;
	snprintf(prev, sizeof prev, "%s", len < 80 ? msg : "");

	while (len > 0)
	{
		n = len > 79 ? 79 : len;
		if (n < len)
			while (n > 20 && msg[n] != ' ')
				n--;
		snprintf(line, sizeof line, "%.*s", n, msg);
		hist_add(line, 0);
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
static int inv_t[40];
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
	int hy, int hx, int lvl),
	{ Module.rp.tiles(scr, vr, t, u, hy, hx, lvl); });

int obj_tile(THING *obj);

/* the page shows a tile set (not None): list rows get icons */
EM_JS(int, js_icons, (void), { return Module.rp.icons(); });

/* Visible window (rvip-wm.js): "M<hex glyph><name>" per monster and
 * "I<hex glyph><name>" per item the screen shows (JS maps the CP437 glyph),
 * then "\t<PC colour>\t<tile>" (tile -1: no icon, the glyph is shown) */
EM_JS(void, js_vis, (const char *s), { Module.rp.vis(UTF8ToString(s)); });
static void
send_visible(unsigned short (*scr)[80])
{
	static char vis[8192], save[MAXSTR];
	char *p = vis, *e = vis + sizeof vis - 100;
	THING *tp;
	int icons = js_icons();

	*p = 0;
	if (prbuf == NULL)
		return;
	memcpy(save, prbuf, MAXSTR);
	for (tp = mlist; tp != NULL && p < e; tp = next(tp))
		if ((scr[tp->t_pos.y][tp->t_pos.x] & 0xff) == tp->t_type)
			p += sprintf(p, "M%02x%.60s\t%d\t%d\n", tp->t_type, monsters[tp->t_type - 'A'].m_name,
				(scr[tp->t_pos.y][tp->t_pos.x] >> 8 & 15) ?: 7, icons ? t_mon[tp->t_type - 'A'] : -1);
	for (tp = lvl_obj; tp != NULL && p < e; tp = next(tp))
		if ((scr[tp->o_pos.y][tp->o_pos.x] & 0xff) == tp->o_type)
			p += sprintf(p, "I%02x%.80s\t%d\t%d\n", tp->o_type,
				tp->o_type == GOLD ? "gold" : inv_name(tp, FALSE),   /* inv_name has no GOLD case: it kept the last name */
				obj_color(tp->o_type),
				icons ? obj_tile(tp) : -1);
	memcpy(prbuf, save, MAXSTR);
	js_vis(vis);
}

/* the text windows of the tiles mode, from the screen and the game's data */
static void
send_text(unsigned short (*scr)[80], int r0, int c0, int r1, int c1)
{
	static char buf[LW], live[LW], last[LW], prompt[LW] = "\1";
	static int pop_on, pop_r0 = -1, pop_c0 = -1;
	int y, n, fg, used, cp_ = -1, cy = 0, cx = 0, nstat, ninv, nmsg;
	const char *a, *b;

	/* cursor: the pop-up, the message row, the status rows */
	if (cur_on && r0 >= 0 && cur_row >= r0 && cur_row <= r1)
		cp_ = P_POP, cy = cur_row - r0, cx = cur_col - c0;
	else if (cur_on && cur_row == 0 && r0 != 0)
		cp_ = P_MSG, cx = cur_col;
	else if (cur_on && cur_row >= 23)
		cp_ = P_STAT, cy = cur_row - 23, cx = cur_col;

	/* Status: rows 23 and 24 */
	for (y = used = 0; y < 2; y++)
	{
		enc_cells(buf, scr[23 + y], 80, &fg);
		wc_line(P_STAT, y, buf);
		wc_rowattr(P_STAT, y, pal[fg], -1);
		if (*buf) used = y + 1;
	}
	nstat = used;

	/* Inventory: the pack, as the game names it */
	n = ninv = inv_lines(inv, inv_at, inv_t, 40, js_icons());
	for (y = 0; y < n; y++)
	{
		enc_str(buf, inv[y]);
		wc_line(P_INV, y, buf);
		wc_rowattr(P_INV, y, pal[inv_at[y] & 15], inv_t[y]);
	}

	/* Messages: the history, then the live row (prompts, --More--) when it
	   is not the last message; also the prompt line over the map */
	live[0] = 0;
	if (r0 != 0)
		enc_cells(live, vram[0], 80, &fg);
	if (strcmp(prompt, live))
	{
		strcpy(prompt, live);
		be_prompt(live);
	}
	for (y = 0; y < nhist; y++)
	{
		enc_str(buf, hist[y]);
		wc_line(P_MSG, y, buf);
		wc_rowattr(P_MSG, y, pal[y == nhist - 1 ? 15 : 7], -1);
	}
	if (nhist)
		enc_str(last, hist[nhist - 1]);
	else
		*last = 0;
	for (a = live; *a == ' '; a++)
		;
	for (b = last; *b == ' '; b++)
		;
	n = nhist;
	if (*a && strcmp(a, b))
	{
		wc_line(P_MSG, n, live);
		wc_rowattr(P_MSG, n, pal[14], -1);
		if (cp_ == P_MSG)
			cy = n;
		n++;
	}
	else if (cp_ == P_MSG)
		cp_ = -1;
	nmsg = n;

	/* the pop-up: a menu's box or a whole text screen over the map */
	if ((r0 >= 0) != pop_on || (r0 >= 0 && (c0 != pop_c0 || r0 != pop_r0)))
	{
		pop_on = r0 >= 0;
		if (cur_p == P_POP)
			wc_cursor(-1, 0, 0);
		wc_reset(P_POP);
		pop_c0 = pop_on ? c0 : -1;
		pop_r0 = pop_on ? r0 : -1;
		be_popup(pop_on, r0, c0);
	}
	if (pop_on)
	{
		for (y = used = 0; y <= r1 - r0; y++)
		{
			enc_cells(buf, vram[r0 + y] + c0, c1 - c0 + 1, &fg);
			wc_line(P_POP, y, buf);
			wc_rowattr(P_POP, y, pal[fg], -1);
			if (*buf) used = y + 1;
		}
	}
	wc_cursor(cp_, cy, cx);
	wc_flush(P_STAT, nstat);
	wc_flush(P_INV, ninv);
	wc_flush(P_MSG, nmsg);
	if (pop_on)
		wc_flush(P_POP, used);
}

void
fe_present(void)
{
	unsigned short (*scr)[80] = is_saved ? saved_vram : vram;
	int r, c, i, r0 = -1, c0 = 0, r1 = -1, c1 = 0;

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
	send_visible(scr);
	send_text(scr, r0, c0, r1, c1);
	js_tiles(scr, vram, map_t, map_u, hero.y, hero.x, level);
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
extern int fe_at_cmd;
EM_JS(int, js_key, (int at_cmd), { return Module.rp.key(at_cmd); });
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
		if ((k = js_key(fe_at_cmd)) >= 0)
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
