/*
 * explore.c - auto-explore and walking to the stairs (RVIP 2026)
 *
 * 'x' explores: breadth-first search over what the screen shows (the player's
 * knowledge), one step per turn, towards the nearest item not yet walked over
 * or the nearest cell next to unexplored space the hero has not stood on.
 * '>' / '<' off the stairs walk to the nearest known staircase and take it.
 * Both stop on a visible monster, any new message and any key; known traps
 * are avoided.
 */

#include "rogue.h"
#include "curses.h"
#include "fe.h"

#define MAPH	(MAXLINES - 3)		/* map rows 1..maxrow-1 */

static int mode;			/* 0, 'x', '>' or '<' */
static int last_msgs;
static bool stood[MAPH + 1][MAXCOLS];	/* hero stood here (this level) */
static bool locked_out;			/* explore found nothing last time */

extern int fe_msgs;

void
explore_reset(void)
{
	memset(stood, 0, sizeof stood);
	mode = 0;
}

void
explore_stop(void)
{
	mode = 0;
}

static byte
scr(int y, int x)
{
	return vram[y][x] & 0xff;
}

static bool
is_item(byte c)
{
	switch (c)
	{
	case GOLD: case POTION: case SCROLL: case FOOD: case STICK:
	case ARMOR: case AMULET: case RING: case WEAPON:
		return TRUE;
	}
	return FALSE;
}

static bool
walkable(int y, int x)
{
	byte c = scr(y, x);

	if (c == ' ')
		return stood[y][x];	/* dark room floor we walked on */
	return c == FLOOR || c == PASSAGE || c == DOOR || c == STAIRS
		|| c == PLAYER || is_item(c) || c == MAGIC || c == BMAGIC;
}

static bool
frontier(int y, int x)
{
	int dy, dx;

	if (stood[y][x])
		return FALSE;
	for (dy = -1; dy <= 1; dy++)
		for (dx = -1; dx <= 1; dx++)
		{
			int ny = y + dy, nx = x + dx;
			if (ny < 1 || ny >= maxrow || nx < 0 || nx >= COLS)
				continue;
			if (scr(ny, nx) == ' ' && !stood[ny][nx])
				return TRUE;
		}
	return FALSE;
}

static bool
target(int y, int x)
{
	byte c = scr(y, x);

	if (mode == '>' || mode == '<')
		return c == STAIRS;
	if (is_item(c))
		return !stood[y][x];
	return frontier(y, x);
}

static bool
monster_in_view(void)
{
	THING *tp;

	for (tp = mlist; tp != NULL; tp = next(tp))
		if (see_monst(tp) && scr(tp->t_pos.y, tp->t_pos.x) == (byte)tp->t_type)
			return TRUE;
	return FALSE;
}

/* first step of a shortest path to a target, or 0 */
static byte
path_step(void)
{
	static const char keys[] = "ykuhlbjn";
	static const int ddy[] = { -1, -1, -1, 0, 0, 1, 1, 1 };
	static const int ddx[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	static short from[MAPH + 1][MAXCOLS];	/* first move index + 1 */
	static coord q[(MAPH + 1) * MAXCOLS];
	int head = 0, tail = 0, d;

	memset(from, 0, sizeof from);
	q[tail++] = hero;
	from[hero.y][hero.x] = 99;
	while (head < tail)
	{
		coord p = q[head++];

		if ((p.y != hero.y || p.x != hero.x) && target(p.y, p.x))
			return keys[from[p.y][p.x] - 1];
		for (d = 0; d < 8; d++)
		{
			coord n;
			n.y = p.y + ddy[d];
			n.x = p.x + ddx[d];
			if (n.y < 1 || n.y >= maxrow || n.x < 0 || n.x >= COLS)
				continue;
			if (from[n.y][n.x] || !walkable(n.y, n.x) || !diag_ok(&p, &n))
				continue;
			from[n.y][n.x] = (p.y == hero.y && p.x == hero.x) ? d + 1 : from[p.y][p.x];
			q[tail++] = n;
		}
	}
	return 0;
}

static bool
stairs_known(void)
{
	int y, x;

	for (y = 1; y < maxrow; y++)
		for (x = 0; x < COLS; x++)
			if (scr(y, x) == STAIRS)
				return TRUE;
	return FALSE;
}

/*
 * Next key of a running explore / stairs walk, or 0 (then the player types)
 */
byte
explore_key(void)
{
	byte k;

	if (!mode)
		return 0;
	stood[hero.y][hero.x] = TRUE;
	if (fe_kbhit())
	{
		fe_flush();
		mode = 0;
		return 0;
	}
	if (fe_msgs != last_msgs || monster_in_view() || no_command)
	{
		mode = 0;
		return 0;
	}
	if ((mode == '>' || mode == '<') && chat(hero.y, hero.x) == STAIRS)
	{
		k = mode;
		mode = 0;
		return k;
	}
	if ((k = path_step()) == 0)
	{
		if (mode == 'x')
		{
			locked_out = TRUE;
			msg(stairs_known()
				? "nothing left to explore; search (s) for secret doors or press > for the stairs"
				: "nothing left to explore; search (s) for secret doors");
		}
		else
			msg("you don't know a way to the stairs");
		mode = 0;
		return 0;
	}
	cur_refresh();
	msleep(30);
	return k;
}

/*
 * The player typed ch: start exploring / walking, or return ch unchanged.
 */
byte
explore_begin(byte ch)
{
	byte k;

	if (ch == 'x')
		mode = 'x';
	else if (ch == '>' || ch == '<')
	{
		if (chat(hero.y, hero.x) == STAIRS || (ch == '<' && !amulet)
		  || !stairs_known())
			return ch;
		mode = ch;
	}
	else
		return ch;
	last_msgs = fe_msgs;
	if (monster_in_view())
	{
		msg("not with a monster in view");
		mode = 0;
		return ' ';
	}
	k = explore_key();
	return k ? k : ' ';
}
