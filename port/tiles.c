/*
 * tiles.c - which ClassicRogue sprite (tiles by Oryx) shows a map cell,
 * and the inventory pane
 */

#include "rogue.h"
#include "curses.h"
#include "fe.h"
#include "tiles.h"

int
obj_tile(THING *obj)
{
	switch (obj->o_type)
	{
	case POTION: return TL_POTION;
	case RING:   return TL_RING;
	case STICK:  return strcmp(ws_type[obj->o_which], "staff") ? TL_WAND : TL_STAFF;
	case SCROLL: return TL_SCROLL;
	case WEAPON: return t_weap[obj->o_which < 10 ? obj->o_which : 0];
	case ARMOR:  return t_arm[obj->o_which < 8 ? obj->o_which : 0];
	case FOOD:   return TL_FOOD;
	case AMULET: return TL_AMULET;
	case GOLD:   return TL_GOLD;
	}
	return -1;
}

/* the room whose outline or inside holds y,x */
static struct room *
room_at(int y, int x)
{
	struct room *rp;

	for (rp = rooms; rp < &rooms[MAXROOMS]; rp++)
		if (!(rp->r_flags & ISGONE) && y >= rp->r_pos.y && x >= rp->r_pos.x
		  && y < rp->r_pos.y + rp->r_max.y && x < rp->r_pos.x + rp->r_max.x)
			return rp;
	return NULL;
}

/* walls are drawn in 3/4 view: the bottom row shows its brick face */
static int
wall_tile(int y, int x, byte ch)
{
	struct room *rp = room_at(y, x);
	int bottom = rp ? y == rp->r_pos.y + rp->r_max.y - 1 : ch == LLWALL || ch == LRWALL;

	switch (ch)
	{
	case VWALL:  return TL_VWALL;
	case LLWALL: return TL_LLWALL;
	case LRWALL: return TL_LRWALL;
	case HWALL:  return bottom ? TL_BWALL : TL_TWALL;
	case ULWALL: return TL_ULWALL;
	case URWALL: return TL_URWALL;
	}
	return TL_TWALL;
}

static int
char_tile(byte ch)
{
	switch (ch)
	{
	case FLOOR:   return TL_FLOOR;
	case PASSAGE: return TL_PASSAGE;
	case DOOR:    return TL_FLOOR;	/* Rogue doors are just gaps in the wall */
	case STAIRS:  return TL_STAIRS;
	case POTION:  return TL_POTION;
	case SCROLL:  return TL_SCROLL;
	case RING:    return TL_RING;
	case STICK:   return TL_WAND;
	case WEAPON:  return t_weap[0];
	case ARMOR:   return t_arm[0];
	case FOOD:    return TL_FOOD;
	case AMULET:  return TL_AMULET;
	case GOLD:    return TL_GOLD;
	}
	return -1;
}

/* floor under a thing at y,x: room floor or passage; -1 in a dark room */
static int
ground(int y, int x)
{
	byte c = chat(y, x);
	struct room *rp;

	if (c == DOOR || c == STAIRS || c == PASSAGE || c == FLOOR)
		return char_tile(c);
	if (c == TRAP)
		return TL_FLOOR;
	if ((rp = room_at(y, x)))
		return (rp->r_flags & ISDARK) ? -1 : TL_FLOOR;
	return TL_PASSAGE;
}

/*
 * Tile for the character ch shown at map position y,x; *under gets the
 * terrain to draw first (or -1). Returns -1 if the cell is drawn as text.
 */
int
tile_for(int y, int x, byte ch, int *under)
{
	THING *tp;
	int t;

	*under = -1;
	if (ch == ' ')
		return -2;		/* nothing known: black */
	if (ch == PLAYER)
	{
		*under = ground(y, x);
		return TL_PLAYER;
	}
	if (ismonster(ch))
	{
		for (tp = mlist; tp != NULL; tp = next(tp))
			if (tp->t_pos.y == y && tp->t_pos.x == x)
			{
				*under = tp->t_oldch == ' ' ? -1 : char_tile(tp->t_oldch);
				if (*under < 0 && tp->t_oldch != ' ')
					*under = ground(y, x);
				break;
			}
		if (tp == NULL)
			*under = ground(y, x);
		return t_mon[ch - 'A'];
	}
	if (ch == TRAP)
	{
		*under = TL_FLOOR;
		return TL_TRAP;
	}
	for (tp = lvl_obj; tp != NULL; tp = next(tp))
		if (tp->o_pos.y == y && tp->o_pos.x == x && tp->o_type == ch)
		{
			*under = ground(y, x);
			return obj_tile(tp);
		}
	if (ch == VWALL || ch == HWALL || ch == ULWALL || ch == URWALL || ch == LLWALL || ch == LRWALL)
		return wall_tile(y, x, ch);
	t = char_tile(ch);
	if (t >= 0 && t != TL_FLOOR && t != TL_PASSAGE && t != TL_DOOR && t != TL_STAIRS)
		*under = ground(y, x);
	return t;
}

/*
 * The inventory pane: one line per pack item, as the game names them
 */
int
inv_lines(char lines[][81], byte attrs[], int max)
{
	THING *obj;
	char save[MAXSTR];
	int n = 0;
	byte ch = 'a';

	if (prbuf == NULL)
		return 0;
	memcpy(save, prbuf, MAXSTR);
	for (obj = pack; obj != NULL && n < max; obj = next(obj), ch++)
	{
		snprintf(lines[n], 81, "%c) %s", ch, inv_name(obj, FALSE));
		attrs[n] = (obj == cur_armor || obj == cur_weapon
			|| obj == cur_ring[LEFT] || obj == cur_ring[RIGHT]) ? 0x0f : 0x07;
		n++;
	}
	memcpy(prbuf, save, MAXSTR);
	return n;
}
