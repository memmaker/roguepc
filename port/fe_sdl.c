/*
 * fe_sdl.c - SDL2 frontend of the Rogue PC port (RVIP 2026)
 *
 * One window, two ways to show the game (button in the top bar, or F12):
 *  - Text:  the original IBM PC text mode, 80x25 cells with the real VGA
 *           9x16 ROM font (CP437) and the CGA/VGA 16-colour palette,
 *           blinking attribute and cursor included. Pixels doubled.
 *  - Tiles: the map as ClassicRogue sprites (tiles by Oryx, 16x24, in the
 *           colours of the original text mode), the message line
 *           and status lines as text, below them a message history and an
 *           inventory pane. Text screens (inventory, help, menus) appear
 *           as a box at their original place over the map.
 * Everything is drawn in a 1440x832 logical canvas that SDL scales with
 * nearest-neighbour only (exactly 2x on a Retina screen).
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "fe.h"
#include "vgafont.h"
#include "tiles.h"

#define W	1440
#define BAR	32
#define H	(BAR + 800)
#define CW	18		/* text cell (2x VGA) */
#define CH	32
#define MAP_X	((W - 80 * TW) / 2)	/* tile cell TW x TH, 1:1 (2x on Retina) */
#define MAP_Y	(BAR + CH + 4)			/* map rows 1..22 */
#define STAT_Y	(MAP_Y + 22 * TH + 4)		/* row 23 (2x), row 24 (1x) */
#define PANE_Y	(STAT_Y + CH + 16 + 6)
#define PW	9		/* pane text cell (1x VGA) */
#define PH	16

int fe_click_row, fe_click_col;
int fe_ingame = 0;
int fe_auto_more = 0;
int fe_msgs = 0;
void port_autosave(void);

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *font_tex, *tile_tex, *splash_tex;
/* second tile set: DawnHack, full colour 16x16 (port/mkdawn.py), same sprite numbers;
   covers every slot the map uses. Drawn stretched to the TW x TH cell. */
static SDL_Texture *dawn_tex;
static unsigned char dawn_has[NTILES];
static int use_dawn = 0;
static void load_dawn(void);
static int tiles_mode = 1, inited = 0, suppress_text = 0, test_mode = 0;
static int npop = 0, pops[4][4];
static int hist_scroll = 0;

static const unsigned char pal[16][3] = {
	{0x00,0x00,0x00}, {0x00,0x00,0xAA}, {0x00,0xAA,0x00}, {0x00,0xAA,0xAA},
	{0xAA,0x00,0x00}, {0xAA,0x00,0xAA}, {0xAA,0x55,0x00}, {0xAA,0xAA,0xAA},
	{0x55,0x55,0x55}, {0x55,0x55,0xFF}, {0x55,0xFF,0x55}, {0x55,0xFF,0xFF},
	{0xFF,0x55,0x55}, {0xFF,0x55,0xFF}, {0xFF,0xFF,0x55}, {0xFF,0xFF,0xFF},
};

/* ---- message history ---------------------------------------------------- */
#define HIST 400
static char hist[HIST][81];
static int nhist = 0;

static void
hist_add(const char *s)
{
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
	char line[81];
	int len = strlen(msg), n;

	while (len > 0)
	{
		n = len > 79 ? 79 : len;
		if (n < len)
			while (n > 20 && msg[n] != ' ')
				n--;
		snprintf(line, sizeof line, "%.*s", n, msg);
		hist_add(line);
		msg += n;
		len -= n;
		while (*msg == ' ')
			msg++, len--;
	}
	hist_scroll = 0;
	fe_msgs++;
}

/* ---- setup -------------------------------------------------------------- */
static void
load_cfg(void)
{
	FILE *f = fopen("roguepc.cfg", "r");
	char buf[64];

	if (f)
	{
		while (fgets(buf, sizeof buf, f))
			if (!strncmp(buf, "mode=", 5))
				tiles_mode = strncmp(buf + 5, "text", 4) != 0;
			else if (!strncmp(buf, "tileset=", 8))
				use_dawn = !strncmp(buf + 8, "dawn", 4);
			else if (!strncmp(buf, "auto_more=", 10))
				fe_auto_more = buf[10] == '1';
		fclose(f);
	}
}

void
fe_save_cfg(void)
{
	FILE *f = fopen("roguepc.cfg", "w");

	if (f)
	{
		fprintf(f, "mode=%s\n", tiles_mode ? "tiles" : "text");
		fprintf(f, "auto_more=%d\n", fe_auto_more);
		fprintf(f, "tileset=%s\n", use_dawn ? "dawn" : "oryx");
		fclose(f);
	}
}

void
fe_init(void)
{
	Uint32 *px;
	int g, y, x;

	if (inited)
		return;
	inited = 1;
	load_cfg();
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		exit(1);
	}
	test_mode = getenv("ROGUEPC_FIFO") != NULL;
	win = SDL_CreateWindow("Rogue PC", SDL_WINDOWPOS_CENTERED, 0, W, H,
		SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE
		| (test_mode ? SDL_WINDOW_HIDDEN : 0));
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!win || !ren)
	{
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_RenderSetLogicalSize(ren, W, H);
	SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

	/* font: 16x16 glyphs of 9x16, white on transparent */
	px = calloc(144 * 256, 4);
	for (g = 0; g < 256; g++)
		for (y = 0; y < 16; y++)
			for (x = 0; x < 9; x++)
				if (vgafont[g][y] >> (8 - x) & 1)
					px[((g / 16) * 16 + y) * 144 + (g % 16) * 9 + x] = 0xffffffff;
	font_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STATIC, 144, 256);
	SDL_UpdateTexture(font_tex, NULL, px, 144 * 4);
	SDL_SetTextureBlendMode(font_tex, SDL_BLENDMODE_BLEND);
	free(px);

	/* tiles: white on transparent, 32 per row, tinted when drawn */
	px = calloc(32 * TW * ((NTILES + 31) / 32 * TH), 4);
	for (g = 0; g < NTILES; g++)
		for (y = 0; y < TH; y++)
			for (x = 0; x < TW; x++)
				if (tile_bits[g][y] >> (15 - x) & 1)
					px[((g / 32) * TH + y) * 32 * TW + (g % 32) * TW + x] = 0xffffffff;
	tile_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STATIC, 32 * TW, (NTILES + 31) / 32 * TH);
	SDL_UpdateTexture(tile_tex, NULL, px, 32 * TW * 4);
	SDL_SetTextureBlendMode(tile_tex, SDL_BLENDMODE_BLEND);
	free(px);
	load_dawn();
	SDL_StartTextInput();
}

/* tiles-dawn.rgba: width, height (4 bytes LE each), RGBA; next to the binary */
static void
load_dawn(void)
{
	const char *p = getenv("ROGUEPC_DAWN");
	FILE *f = fopen(p ? p : "../port/tiles-dawn.rgba", "rb");
	unsigned char hd[8], *px;
	int w, h, t;

	if (!f)
		return;
	if (fread(hd, 1, 8, f) == 8 && (w = hd[0] | hd[1] << 8 | hd[2] << 16 | hd[3] << 24) == 32 * 16
	  && (h = hd[4] | hd[5] << 8 | hd[6] << 16 | hd[7] << 24) > 0 && h <= 64 * 16
	  && (px = malloc((size_t)w * h * 4)) != NULL)
	{
		if (fread(px, 4, (size_t)w * h, f) == (size_t)w * h)
		{
			for (t = 0; t < NTILES && t / 32 * 16 < h; t++)	/* covered: alpha in the middle */
				dawn_has[t] = px[(((t / 32) * 16 + 8) * w + (t % 32) * 16 + 8) * 4 + 3] != 0;
			dawn_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
			SDL_UpdateTexture(dawn_tex, NULL, px, w * 4);
			SDL_SetTextureBlendMode(dawn_tex, SDL_BLENDMODE_BLEND);
		}
		free(px);
	}
	fclose(f);
}

/* ---- drawing helpers ---------------------------------------------------- */
static void
fill(int x, int y, int w, int h, int c)
{
	SDL_Rect r = { x, y, w, h };

	SDL_SetRenderDrawColor(ren, pal[c][0], pal[c][1], pal[c][2], 255);
	SDL_RenderFillRect(ren, &r);
}

static void
glyph(int x, int y, int w, int h, unsigned char g, int fg)
{
	SDL_Rect s = { (g % 16) * 9, (g / 16) * 16, 9, 16 }, d = { x, y, w, h };

	if (g == ' ' || g == 0)
		return;
	SDL_SetTextureColorMod(font_tex, pal[fg][0], pal[fg][1], pal[fg][2]);
	SDL_RenderCopy(ren, font_tex, &s, &d);
}

static int
blink_on(void)
{
	return (SDL_GetTicks() / 229) & 1;
}

/* one text cell (attr << 8 | ch) at pixel x,y, cell size w x h */
static void
cell(int x, int y, int w, int h, unsigned short v)
{
	int a = v >> 8, fg = a & 15;

	fill(x, y, w, h, (a >> 4) & 7);
	if (!(a & 0x80) || blink_on())
		glyph(x, y, w, h, v & 0xff, fg);
}

static void
text(int x, int y, const char *s, int fg, int scale)
{
	for (; *s; s++, x += 9 * scale)
		glyph(x, y, 9 * scale, 16 * scale, (unsigned char)*s, fg);
}

static void
tile(int t, int x, int y, int c)
{
	SDL_Rect s = { (t % 32) * TW, (t / 32) * TH, TW, TH }, d = { x, y, TW, TH };

	if (use_dawn && dawn_tex && dawn_has[t])
	{
		SDL_Rect ds = { (t % 32) * 16, (t / 32) * 16, 16, 16 };
		SDL_RenderCopy(ren, dawn_tex, &ds, &d);
		return;
	}
	SDL_SetTextureColorMod(tile_tex, pal[c][0], pal[c][1], pal[c][2]);
	SDL_RenderCopy(ren, tile_tex, &s, &d);
}

/* ---- the two layouts ---------------------------------------------------- */
static void
draw_cursor(void)
{
	int a;

	if (!cur_on || !((SDL_GetTicks() / 115) & 1))
		return;
	a = vram[cur_row][cur_col] >> 8;
	fill(cur_col * CW, BAR + cur_row * CH + 26, CW, 4, a & 15 ? a & 15 : 7);
}

static void
draw_text_screen(unsigned short (*scr)[80])
{
	int r, c;

	for (r = 0; r < 25; r++)
		for (c = 0; c < 80; c++)
			cell(c * CW, BAR + r * CH, CW, CH, scr[r][c]);
}

/* bounding box of what a text screen shows */
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

static void
draw_map(unsigned short (*scr)[80])
{
	int r, c, t, under;

	fill(0, MAP_Y, W, 22 * TH, 0);
	fill(MAP_X - 2, MAP_Y - 2, 80 * TW + 4, 1, 8);
	fill(MAP_X - 2, MAP_Y + 22 * TH + 1, 80 * TW + 4, 1, 8);
	for (r = 1; r <= 22; r++)
		for (c = 0; c < 80; c++)
		{
			unsigned short v = scr[r][c];
			int x = MAP_X + c * TW, y = MAP_Y + (r - 1) * TH, a = v >> 8;

			t = tile_for(r, c, v & 0xff, &under);
			if (t == -2)
				continue;
			if (under >= 0)
				tile(under, x, y, tile_col[under]);
			if (t >= 0)	/* the cell's text-mode colour (stairs: black on green) */
				tile(t, x, y, a & 15 ? a & 15 : (a >> 4) & 7);
			else
			{
				fill(x, y, TW, TH, (a >> 4) & 7);
				glyph(x + 3, y + 4, 9, 16, v & 0xff, a & 15);
			}
		}
}

static void
pane_title(int x, const char *t)
{
	fill(x, PANE_Y, 720, PH, 1);
	text(x + 9, PANE_Y, t, 15, 1);
}

static void
draw_panes(void)
{
	static char inv[40][81];
	static unsigned char at[40];
	int rows = (H - PANE_Y - PH) / PH, i, n, first;

	fill(0, PANE_Y - 4, W, H - PANE_Y + 4, 0);
	fill(0, PANE_Y - 3, W, 1, 8);
	fill(719, PANE_Y, 1, H - PANE_Y, 8);

	pane_title(0, "Messages");
	first = nhist - rows - hist_scroll;
	if (first < 0)
		first = 0;
	for (i = 0; i < rows && first + i < nhist; i++)
		text(9, PANE_Y + PH * (i + 1), hist[first + i],
			first + i == nhist - 1 ? 15 : 7, 1);

	pane_title(721, "Inventory");
	n = inv_lines(inv, at, 40);
	for (i = 0; i < n; i++)
	{
		char buf[80];
		if (n <= rows)
			snprintf(buf, sizeof buf, "%.78s", inv[i]);
		else
			snprintf(buf, sizeof buf, "%.39s", inv[i]);
		text(721 + 9 + (i / rows) * 360, PANE_Y + PH * (i % rows + 1), buf, at[i], 1);
	}
}

static void
draw_popup(int r0, int c0, int r1, int c1, int frame)
{
	int r, c, x0 = c0 * CW - 6 * frame, y0 = BAR + r0 * CH - 4 * frame;
	int w = (c1 - c0 + 1) * CW + 12 * frame, h = (r1 - r0 + 1) * CH + 8 * frame;

	fill(x0 + 6, y0 + 6, w, h, 0);		/* shadow */
	if (frame)
		fill(x0 - 2, y0 - 2, w + 4, h + 4, 7);	/* border */
	fill(x0, y0, w, h, 0);
	for (r = r0; r <= r1; r++)
		for (c = c0; c <= c1; c++)
			cell(c * CW, BAR + r * CH, CW, CH, vram[r][c]);
	if (cur_on && cur_row >= r0 && cur_row <= r1)
		draw_cursor();
}

static void
draw_tiles_screen(void)
{
	int c, r0, c0, r1, c1;

	for (c = 0; c < 80; c++)
		cell(c * CW, BAR, CW, CH, vram[0][c]);
	draw_map(is_saved ? saved_vram : vram);
	for (c = 0; c < 80; c++)
	{
		cell(c * CW, STAT_Y, CW, CH, (is_saved ? saved_vram : vram)[23][c]);
		cell(W - 8 - (80 - c) * 9, STAT_Y + CH, 9, 16, (is_saved ? saved_vram : vram)[24][c]);
	}
	draw_panes();
	if (npop > 0)
		for (int i = 0; i < npop && i < 4; i++)
			draw_popup(pops[i][0], pops[i][1], pops[i][2], pops[i][3], 0);
	else if (is_saved && screen_bbox(&r0, &c0, &r1, &c1))
		draw_popup(r0, c0, r1, c1, 1);
	else if (cur_on && cur_row == 0)
		draw_cursor();
}

/* ---- top bar ------------------------------------------------------------ */
static SDL_Rect btn_tiles = { W - 200, 4, 90, 24 }, btn_text = { W - 104, 4, 90, 24 };
static SDL_Rect btn_more = { W - 370, 4, 150, 24 }, btn_set = { W - 560, 4, 180, 24 };

static void
button(SDL_Rect *b, const char *label, int active)
{
	fill(b->x, b->y, b->w, b->h, active ? 7 : 8);
	fill(b->x + 1, b->y + 1, b->w - 2, b->h - 2, active ? 15 : 0);
	text(b->x + (b->w - 9 * (int)strlen(label)) / 2, b->y + 4, label, active ? 0 : 7, 1);
}

static void
draw_bar(void)
{
	fill(0, 0, W, BAR, 1);
	text(12, 8, "ROGUE  The Adventure Game", 15, 1);
	text(270, 8, "Enter: all commands   x: explore   F12: tiles/text", 11, 1);
	button(&btn_more, fe_auto_more ? "auto_more: on" : "auto_more: off", fe_auto_more);
	if (dawn_tex)
		button(&btn_set, use_dawn ? "Tile set: DawnHack" : "Tile set: Oryx", 0);
	button(&btn_tiles, "Tiles", tiles_mode);
	button(&btn_text, "Text", !tiles_mode);
}

/* ---- CGA title picture -------------------------------------------------- */
void
fe_splash(const char *path)
{
	static const Uint32 cga[4] = { 0xff000000, 0xff55ffff, 0xffff55ff, 0xffffffff };
	unsigned char data[7 + 16384];
	Uint32 *px;
	FILE *f;
	int y, x;

	fe_init();
	if (splash_tex)
	{
		SDL_DestroyTexture(splash_tex);
		splash_tex = NULL;
	}
	if (!path || !(f = fopen(path, "rb")))
		return;
	memset(data, 0, sizeof data);
	fread(data, 1, sizeof data, f);
	fclose(f);
	px = malloc(320 * 200 * 4);
	for (y = 0; y < 200; y++)
		for (x = 0; x < 320; x++)
		{
			unsigned char b = data[7 + (y & 1) * 8192 + (y / 2) * 80 + x / 4];
			px[y * 320 + x] = cga[(b >> (6 - 2 * (x & 3))) & 3];
		}
	splash_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STATIC, 320, 200);
	SDL_UpdateTexture(splash_tex, NULL, px, 320 * 4);
	free(px);
	fe_present();
}

/* ---- present ------------------------------------------------------------ */
static void
render(void)
{
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	draw_bar();
	if (splash_tex)
	{
		SDL_Rect d = { (W - 1280) / 2, BAR, 1280, 800 };
		SDL_RenderCopy(ren, splash_tex, NULL, &d);
	}
	else if (curtain_down)
		draw_text_screen(curtain_vram);
	else if (tiles_mode && fe_ingame)
		draw_tiles_screen();
	else
	{
		draw_text_screen(vram);
		draw_cursor();
	}
}

void
fe_present(void)
{
	fe_init();
	render();
	SDL_RenderPresent(ren);
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

/* ---- input -------------------------------------------------------------- */
static int
keypad(SDL_Scancode sc)
{
	switch (sc)
	{
	case SDL_SCANCODE_KP_0: return FK_KP0;
	case SDL_SCANCODE_KP_1: return FK_KP1;
	case SDL_SCANCODE_KP_2: return FK_KP2;
	case SDL_SCANCODE_KP_3: return FK_KP3;
	case SDL_SCANCODE_KP_4: return FK_KP4;
	case SDL_SCANCODE_KP_5: return FK_KP5;
	case SDL_SCANCODE_KP_6: return FK_KP6;
	case SDL_SCANCODE_KP_7: return FK_KP7;
	case SDL_SCANCODE_KP_8: return FK_KP8;
	case SDL_SCANCODE_KP_9: return FK_KP9;
	case SDL_SCANCODE_KP_PERIOD: return FK_KPDOT;
	case SDL_SCANCODE_KP_ENTER: return FK_KPENTER;
	case SDL_SCANCODE_KP_PLUS: return FK_KPPLUS;
	case SDL_SCANCODE_KP_MINUS: return FK_KPMINUS;
	case SDL_SCANCODE_KP_MULTIPLY: return FK_KPSTAR;
	case SDL_SCANCODE_KP_DIVIDE: return FK_KPSLASH;
	default: return 0;
	}
}

void
fe_toggle_mode(void)
{
	tiles_mode = !tiles_mode;
	fe_save_cfg();
	fe_present();
}

static void
quit_now(void)
{
	port_autosave();
	SDL_Quit();
	exit(0);
}

static int
in(SDL_Rect *b, int x, int y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

/* one event -> key code, or -1 */
static int
event_key(SDL_Event *ev)
{
	SDL_Keycode k;
	int m, kp;

	switch (ev->type)
	{
	case SDL_QUIT:
		quit_now();
		break;
	case SDL_WINDOWEVENT:
		fe_present();
		break;
	case SDL_MOUSEWHEEL:
		if (tiles_mode && fe_ingame && !is_saved)
		{
			hist_scroll += ev->wheel.y > 0 ? 3 : -3;
			if (hist_scroll < 0) hist_scroll = 0;
			if (hist_scroll > nhist) hist_scroll = nhist;
			fe_present();
			return -1;
		}
		return ev->wheel.y > 0 ? FK_WHEELUP : FK_WHEELDOWN;
	case SDL_MOUSEBUTTONDOWN:
		if (ev->button.button != SDL_BUTTON_LEFT)
			return ev->button.button == SDL_BUTTON_RIGHT ? 27 : -1;
		if (in(&btn_more, ev->button.x, ev->button.y))
		{
			fe_auto_more = !fe_auto_more;
			fe_save_cfg();
			fe_present();
		}
		else if (dawn_tex && in(&btn_set, ev->button.x, ev->button.y))
		{
			use_dawn = !use_dawn;
			fe_save_cfg();
			fe_present();
		}
		else if (in(&btn_tiles, ev->button.x, ev->button.y) && !tiles_mode)
			fe_toggle_mode();
		else if (in(&btn_text, ev->button.x, ev->button.y) && tiles_mode)
			fe_toggle_mode();
		else if (ev->button.y >= BAR)
		{
			fe_click_row = (ev->button.y - BAR) / CH;
			fe_click_col = ev->button.x / CW;
			return FK_CLICK;
		}
		return -1;
	case SDL_TEXTINPUT:
		if (suppress_text)
		{
			suppress_text = 0;
			return -1;
		}
		if ((unsigned char)ev->text.text[0] < 0x80 && ev->text.text[0] >= ' ')
			return ev->text.text[0];
		return -1;
	case SDL_KEYDOWN:
		k = ev->key.keysym.sym;
		m = ev->key.keysym.mod;
		suppress_text = 0;
		if ((kp = keypad(ev->key.keysym.scancode)))
		{
			suppress_text = 1;
			return kp;
		}
		if (m & KMOD_GUI)
		{
			if (k == SDLK_q)
				quit_now();
			suppress_text = 1;
			return -1;
		}
		if ((m & KMOD_CTRL) && k >= SDLK_a && k <= SDLK_z)
		{
			suppress_text = 1;
			return k - SDLK_a + 1;
		}
		switch (k)
		{
		case SDLK_RETURN: return '\n';
		case SDLK_ESCAPE: return 27;
		case SDLK_BACKSPACE: return '\b';
		case SDLK_UP: return FK_UP;
		case SDLK_DOWN: return FK_DOWN;
		case SDLK_LEFT: return FK_LEFT;
		case SDLK_RIGHT: return FK_RIGHT;
		case SDLK_HOME: return FK_HOME;
		case SDLK_END: return FK_END;
		case SDLK_PAGEUP: return FK_PGUP;
		case SDLK_PAGEDOWN: return FK_PGDN;
		case SDLK_INSERT: return FK_INS;
		case SDLK_DELETE: return FK_DEL;
		case SDLK_F12: fe_toggle_mode(); return -1;
		}
		if (k >= SDLK_F1 && k <= SDLK_F10)
		{
			if (k == SDLK_F9 && (m & KMOD_ALT))
				return FK_ALTF9;
			return FK_F1 + (k - SDLK_F1);
		}
		return -1;
	}
	return -1;
}

/* ---- test hook (RVIP: no global keys/screenshots while testing) ----------
 * ROGUEPC_FIFO=<fifo>: bytes read from it are keys; 0xFF 'S' writes a
 * screenshot to $ROGUEPC_SHOT.bmp and the text screen to $ROGUEPC_SHOT.txt,
 * 0xFF 'K' <n> sends key 0x100+n, 0xFF 'T' toggles tiles/text. */
static int fifo_fd = -2;

static void
shot(void)
{
	const char *base = getenv("ROGUEPC_SHOT");
	char path[512];
	SDL_Surface *s;
	FILE *f;
	int w, h, r, c;

	if (!base)
		return;
	render();
	SDL_GetRendererOutputSize(ren, &w, &h);
	s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
	snprintf(path, sizeof path, "%s.bmp", base);
	SDL_SaveBMP(s, path);
	SDL_FreeSurface(s);
	SDL_RenderPresent(ren);
	snprintf(path, sizeof path, "%s.txt", base);
	if ((f = fopen(path, "w")))
	{
		for (r = 0; r < 25; r++)
		{
			for (c = 0; c < 80; c++)
			{
				int ch = vram[r][c] & 0xff;
				fputc(ch >= 32 && ch < 127 ? ch : ch == 0 ? ' ' : '~', f);
			}
			fputc('\n', f);
		}
		fprintf(f, "cursor %d,%d on=%d saved=%d ingame=%d tiles=%d\n",
			cur_row, cur_col, cur_on, is_saved, fe_ingame, tiles_mode);
		fclose(f);
	}
}


static int
fifo_key(void)
{
	unsigned char b[2];
	const char *p;

	if (fifo_fd == -2)
		fifo_fd = (p = getenv("ROGUEPC_FIFO")) ? open(p, O_RDWR | O_NONBLOCK) : -1;
	if (fifo_fd < 0 || read(fifo_fd, b, 1) != 1)
		return -1;
	if (b[0] != 0xff)
		return b[0] == '\r' ? '\n' : b[0];
	while (read(fifo_fd, b, 1) != 1)
		SDL_Delay(1);
	if (b[0] == 'S')
		shot();
	else if (b[0] == 'T')
		fe_toggle_mode();
	else if (b[0] == 'K')
	{
		while (read(fifo_fd, b + 1, 1) != 1)
			SDL_Delay(1);
		return 0x100 + b[1];
	}
	return -1;
}

int
fe_getkey(int msdelay)
{
	Uint32 start = SDL_GetTicks();
	int phase = -1, key, wait;
	SDL_Event ev;

	fe_init();
	for (;;)
	{
		int p = (SDL_GetTicks() / 115) & 3;
		if (p != phase)
		{
			phase = p;
			fe_present();
		}
		wait = 40;
		if (msdelay >= 0)
		{
			int left = msdelay - (int)(SDL_GetTicks() - start);
			if (left <= 0)
				return -1;
			if (left < wait)
				wait = left;
		}
		if ((key = fifo_key()) != -1)
			return key;
		if (SDL_WaitEventTimeout(&ev, wait) && !(test_mode && ev.type != SDL_QUIT)
		  && (key = event_key(&ev)) != -1)
			return key;
	}
}

int
fe_kbhit(void)
{
	SDL_PumpEvents();
	if (test_mode)
		return 0;
	return SDL_HasEvent(SDL_KEYDOWN) || SDL_HasEvent(SDL_TEXTINPUT)
		|| SDL_HasEvent(SDL_MOUSEBUTTONDOWN) || SDL_HasEvent(SDL_QUIT);
}

/* forget typed-ahead keys (the key that stopped auto-explore) */
void
fe_flush(void)
{
	SDL_PumpEvents();
	SDL_FlushEvents(SDL_KEYDOWN, SDL_TEXTINPUT);
	SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);
	suppress_text = 0;
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

	if (!inited || !*msg)
		return;
	while (*msg == '\n')
		msg++;
	snprintf(line, sizeof line, "%s", msg);
	for (c = 0; line[c]; c++)
		if (line[c] == '\n')
			line[c] = ' ';
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
