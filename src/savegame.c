/*
 * savegame.c - save and restore for the RVIP port (2026)
 *
 * The 1985 code saved the whole DOS data segment and heap and relied on them
 * loading at the same addresses; upstream disabled it. This writes every piece
 * of game state explicitly. Pointers are stored as (region, offset): into
 * _things, the player, rooms, passages, _guesses and a few buffers; any other
 * string (damage dice, potion colours, ...) is stored by its text. Daemon and
 * fuse functions are stored by name (daemon.c).
 */

#include "rogue.h"
#include "curses.h"
#include "fe.h"
#include <stddef.h>
#ifdef __EMSCRIPTEN__
static bool web_saved;
#endif

#define SAVE_MAGIC	"RoguePC-RVIP-1"

enum { P_NULL, P_THINGS, P_PLAYER, P_ROOMS, P_PASSAGES, P_GUESSES,
	P_FDAMAGE, P_MACRO, P_NULLSTR, P_WHOAMI, P_PRBUF, P_STR };

extern int bwflag;
extern char whoami[24], fruit[24], macro[42], f_damage[10];
extern unsigned short vram[25][80];
/* complete the array types for sizeof (see extern.c) */
extern struct room rooms[MAXROOMS], passages[MAXPASS];
extern struct array s_names[MAXSCROLLS], _guesses[MAXSCROLLS+MAXPOTIONS+MAXRINGS+MAXSTICKS];
extern bool s_know[MAXSCROLLS], p_know[MAXPOTIONS], r_know[MAXRINGS], ws_know[MAXSTICKS];
extern char huh[BUFSIZE];
extern int a_chances[MAXARMORS], a_class[MAXARMORS];
int	daemon_save(FILE *f);
int	daemon_load(FILE *f);

/* ---- strings stored by text ---------------------------------------------- */
#define MAXSTRS 256
static char *strs[MAXSTRS];
static int nstrs;

static int
str_index(const char *s)
{
	int i;

	for (i = 0; i < nstrs; i++)
		if (!strcmp(strs[i], s))
			return i;
	if (nstrs == MAXSTRS)
		return 0;
	strs[nstrs] = strdup(s);
	return nstrs++;
}

/* ---- pointer encoding ---------------------------------------------------- */
#define IN(p, base, size)	((char *)(p) >= (char *)(base) && (char *)(p) < (char *)(base) + (size))
/* the code sits in the top bits of a pointer-sized value (wasm32: 4 bytes) */
#define TAGSHIFT		(sizeof(void *) == 8 ? 48 : 24)
#define ENC(tag, off)		(((uint64_t)(tag) << TAGSHIFT) | (uint64_t)(off))

static uint64_t
enc(const void *p)
{
	if (p == NULL)
		return ENC(P_NULL, 0);
	if (IN(p, _things, sizeof(THING) * MAXITEMS))
		return ENC(P_THINGS, (char *)p - (char *)_things);
	if (IN(p, &player, sizeof player))
		return ENC(P_PLAYER, (char *)p - (char *)&player);
	if (IN(p, rooms, sizeof rooms))
		return ENC(P_ROOMS, (char *)p - (char *)rooms);
	if (IN(p, passages, sizeof passages))
		return ENC(P_PASSAGES, (char *)p - (char *)passages);
	if (IN(p, _guesses, sizeof _guesses))
		return ENC(P_GUESSES, (char *)p - (char *)_guesses);
	if (IN(p, f_damage, sizeof f_damage))
		return ENC(P_FDAMAGE, (char *)p - f_damage);
	if (IN(p, macro, MACROSZ))
		return ENC(P_MACRO, (char *)p - macro);
	if (p == nullstr)
		return ENC(P_NULLSTR, 0);
	if (p == whoami)
		return ENC(P_WHOAMI, 0);
	if (prbuf && IN(p, prbuf, MAXSTR))
		return ENC(P_PRBUF, (char *)p - prbuf);
	return ENC(P_STR, str_index(p));	/* a string: keep its text */
}

static void *
dec(uint64_t v)
{
	size_t off = v & ((1ULL << TAGSHIFT) - 1);

	switch ((int)(v >> TAGSHIFT))
	{
	case P_THINGS:   return (char *)_things + off;
	case P_PLAYER:   return (char *)&player + off;
	case P_ROOMS:    return (char *)rooms + off;
	case P_PASSAGES: return (char *)passages + off;
	case P_GUESSES:  return (char *)_guesses + off;
	case P_FDAMAGE:  return f_damage + off;
	case P_MACRO:    return macro + off;
	case P_NULLSTR:  return nullstr;
	case P_WHOAMI:   return whoami;
	case P_PRBUF:    return prbuf + off;
	case P_STR:      return off < (size_t)nstrs ? strs[off] : nullstr;
	}
	return NULL;
}

/* pointer fields of a THING, by what it is */
static const size_t mon_ptrs[] = {
	offsetof(THING, _t._l_next), offsetof(THING, _t._l_prev),
	offsetof(THING, _t._t_dest), offsetof(THING, _t._t_stats.s_dmg),
	offsetof(THING, _t._t_room), offsetof(THING, _t._t_pack),
};
static const size_t obj_ptrs[] = {
	offsetof(THING, _o._l_next), offsetof(THING, _o._l_prev),
	offsetof(THING, _o._o_text), offsetof(THING, _o._o_damage),
	offsetof(THING, _o._o_hurldmg),
};

static void
thing_ptrs(THING *t, int kind, bool encode)
{
	const size_t *offs = kind == 1 ? mon_ptrs : obj_ptrs;
	int n = kind == 1 ? 6 : 5, i;

	for (i = 0; i < n; i++)
	{
		void **pp = (void **)((char *)t + offs[i]);
		if (encode)
		{
			uintptr_t u = (uintptr_t)enc(*pp);
			memcpy(pp, &u, sizeof u);
		}
		else
		{
			uintptr_t u;
			memcpy(&u, pp, sizeof u);
			*pp = dec(u);
		}
	}
}

/* 1 = monster, 2 = object, for every slot of _things */
static void
thing_kinds(byte *kind)
{
	THING *tp, *op;

	memset(kind, 0, MAXITEMS);
	for (tp = mlist; tp != NULL; tp = next(tp))
	{
		kind[tp - _things] = 1;
		for (op = tp->t_pack; op != NULL; op = next(op))
			kind[op - _things] = 2;
	}
	for (op = lvl_obj; op != NULL; op = next(op))
		kind[op - _things] = 2;
	for (op = pack; op != NULL; op = next(op))
		kind[op - _things] = 2;
}

/* ---- file helpers -------------------------------------------------------- */
static int ok;
static FILE *fp;

static void
W(const void *p, size_t n)
{
	if (ok && fwrite(p, n, 1, fp) != 1)
		ok = FALSE;
}

static void
R(void *p, size_t n)
{
	if (ok && fread(p, n, 1, fp) != 1)
		ok = FALSE;
}

/*
 * Every global of the game that isn't a pointer, in one list so save and
 * restore can't disagree
 */
#define PLAIN(X) \
	X(maxitems) X(reinit) X(after) X(noscore) X(again) X(s_know) X(p_know) \
	X(r_know) X(ws_know) X(amulet) X(saw_amulet) X(door_stop) X(fastmode) \
	X(faststate) X(firstmove) X(playing) X(running) X(save_msg) X(terse) \
	X(expert) X(was_trapped) X(bailout) X(take) X(runch) X(s_names) \
	X(huh) X(_guesses) X(iguess) X(maxrow) X(max_level) X(ntraps) X(dnum) \
	X(level) X(purse) X(mpos) X(no_move) X(no_command) X(inpack) X(total) \
	X(no_food) X(count) X(fung_hit) X(quiet) X(food_left) X(group) \
	X(hungry_state) X(seed) X(hit_mul) X(goodchk) X(oldpos) X(delta) \
	X(rooms) X(passages) X(f_damage) X(a_chances) X(a_class) X(whoami) \
	X(fruit) X(macro) X(bwflag)

#define WRITE_V(v)	W(&v, sizeof v);
#define READ_V(v)	R(&v, sizeof v);

static void
magic_tables(bool save)
{
	struct { struct magic_item *t; int n; } tabs[] = {
		{ things, NUMTHINGS }, { s_magic, MAXSCROLLS }, { p_magic, MAXPOTIONS },
		{ r_magic, MAXRINGS }, { ws_magic, MAXSTICKS },
	};
	int i, j;

	for (i = 0; i < 5; i++)
		for (j = 0; j < tabs[i].n; j++)
		{
			if (save)
			{
				W(&tabs[i].t[j].mi_prob, sizeof tabs[i].t[j].mi_prob);
				W(&tabs[i].t[j].mi_worth, sizeof tabs[i].t[j].mi_worth);
			}
			else
			{
				R(&tabs[i].t[j].mi_prob, sizeof tabs[i].t[j].mi_prob);
				R(&tabs[i].t[j].mi_worth, sizeof tabs[i].t[j].mi_worth);
			}
		}
}

#define STRPTRS(X) \
	X(p_colors, MAXPOTIONS) X(r_stones, MAXRINGS) X(ws_made, MAXSTICKS) \
	X(ws_type, MAXSTICKS) X(s_guess, MAXSCROLLS) X(p_guess, MAXPOTIONS) \
	X(r_guess, MAXRINGS) X(ws_guess, MAXSTICKS)

/* ---- save ------------------------------------------------------------------ */
int
port_save(FILE *f)
{
	static THING things_copy[MAXITEMS];
	THING player_copy;
	byte kind[MAXITEMS];
	int i, sz = sizeof(THING), maxi = MAXITEMS;

	fp = f;
	ok = TRUE;
	nstrs = 0;
	W(SAVE_MAGIC, sizeof SAVE_MAGIC);
	W(&sz, sizeof sz);
	W(&maxi, sizeof maxi);
	W(&revno, sizeof revno);
	W(&verno, sizeof verno);

	/* strings are collected while encoding: body first into memory */
	thing_kinds(kind);
	memcpy(things_copy, _things, sizeof things_copy);
	for (i = 0; i < MAXITEMS; i++)
		if (_t_alloc[i] && kind[i])
			thing_ptrs(&things_copy[i], kind[i], TRUE);
	player_copy = player;
	thing_ptrs(&player_copy, 1, TRUE);
	{
		/* pointer globals, encoded now so their strings are known */
		uint64_t gp[160];
		int n = 0;
#define ENC_ARR(a, cnt)	for (i = 0; i < cnt; i++) gp[n++] = enc(a[i]);
		STRPTRS(ENC_ARR)
		gp[n++] = enc(max_stats.s_dmg);
		gp[n++] = enc(cur_armor);
		gp[n++] = enc(cur_ring[LEFT]);
		gp[n++] = enc(cur_ring[RIGHT]);
		gp[n++] = enc(cur_weapon);
		gp[n++] = enc(oldrp);
		gp[n++] = enc(lvl_obj);
		gp[n++] = enc(mlist);
		gp[n++] = enc(typebuf);

		/* string table */
		W(&nstrs, sizeof nstrs);
		for (i = 0; i < nstrs; i++)
		{
			int l = strlen(strs[i]) + 1;
			W(&l, sizeof l);
			W(strs[i], l);
		}
		W(&n, sizeof n);
		W(gp, n * sizeof gp[0]);
	}
	PLAIN(WRITE_V)
	magic_tables(TRUE);
	W(_t_alloc, MAXITEMS * sizeof(int));
	W(kind, MAXITEMS);
	W(things_copy, sizeof things_copy);
	W(&player_copy, sizeof player_copy);
	W(_level, (MAXLINES - 3) * MAXCOLS);
	W(_flags, (MAXLINES - 3) * MAXCOLS);
	W(&max_stats, sizeof max_stats);
	W(vram, sizeof(unsigned short) * 25 * 80);
	if (ok)
		ok = daemon_save(f);
	return ok;
}

/* ---- restore -------------------------------------------------------------- */
int
port_restore(FILE *f)
{
	char magic[sizeof SAVE_MAGIC];
	byte kind[MAXITEMS];
	uint64_t gp[160];
	int i, n, sz, maxi, rev, ver;
	char *save_dmg;

	fp = f;
	ok = TRUE;
	R(magic, sizeof magic);
	R(&sz, sizeof sz);
	R(&maxi, sizeof maxi);
	R(&rev, sizeof rev);
	R(&ver, sizeof ver);
	if (!ok || memcmp(magic, SAVE_MAGIC, sizeof magic) || sz != (int)sizeof(THING)
	  || maxi != MAXITEMS || rev != revno || ver != verno)
		return FALSE;

	R(&nstrs, sizeof nstrs);
	if (nstrs < 0 || nstrs > MAXSTRS)
		return FALSE;
	for (i = 0; i < nstrs && ok; i++)
	{
		int l = 0;
		R(&l, sizeof l);
		if (l <= 0 || l > 4096)
			return FALSE;
		strs[i] = malloc(l);
		R(strs[i], l);
		strs[i][l - 1] = 0;
	}
	R(&n, sizeof n);
	if (n < 0 || n > 160)
		return FALSE;
	R(gp, n * sizeof gp[0]);

	PLAIN(READ_V)
	magic_tables(FALSE);
	R(_t_alloc, MAXITEMS * sizeof(int));
	R(kind, MAXITEMS);
	R(_things, sizeof(THING) * MAXITEMS);
	R(&player, sizeof player);
	R(_level, (MAXLINES - 3) * MAXCOLS);
	R(_flags, (MAXLINES - 3) * MAXCOLS);
	R(&max_stats, sizeof max_stats);
	R(vram, sizeof(unsigned short) * 25 * 80);
	if (!ok)
		return FALSE;
	for (i = 0; i < MAXITEMS; i++)
		if (_t_alloc[i] && kind[i])
			thing_ptrs(&_things[i], kind[i], FALSE);
	thing_ptrs(&player, 1, FALSE);

	n = 0;
#define DEC_ARR(a, cnt)	for (i = 0; i < cnt; i++) a[i] = dec(gp[n++]);
	STRPTRS(DEC_ARR)
	save_dmg = dec(gp[n++]);
	max_stats.s_dmg = save_dmg;
	cur_armor = dec(gp[n++]);
	cur_ring[LEFT] = dec(gp[n++]);
	cur_ring[RIGHT] = dec(gp[n++]);
	cur_weapon = dec(gp[n++]);
	oldrp = dec(gp[n++]);
	lvl_obj = dec(gp[n++]);
	mlist = dec(gp[n++]);
	typebuf = dec(gp[n++]);

	/* copy protection was passed when the game started */
	your_na = whoami;
	kild_by = prbuf;
	return daemon_load(f);
}

/* ---- the commands ---------------------------------------------------------- */
static bool
write_save(const char *name)
{
	FILE *f = fopen(name, "wb");
	bool good;

	if (f == NULL)
		return FALSE;
	good = port_save(f);
	if (fclose(f) != 0)
		good = FALSE;
	if (!good)
		remove(name);
	return good;
}

void
save_game(void)
{
	char savename[20];
	int ret;

	msg("");
	mpos = 0;
	move(0, 0);
	printw("Save file (press Enter for \"%s\") ? ", s_save);
	ret = getinfo(savename, 19);
	move(0, 0);
	clrtoeol();
	if (ret == ESCAPE)
	{
		after = FALSE;
		return;
	}
	if (*savename == 0)
		strcpy(savename, s_save);
	if (!write_save(savename))
	{
		msg("could not write %s", savename);
		after = FALSE;
		return;
	}
#ifdef __EMSCRIPTEN__
	web_saved = TRUE;
#endif
	fatal("Game saved as %s.", savename);
}

#ifdef __EMSCRIPTEN__
static double web_last;

void
port_web_autosave(void)
{
	if (!fe_ingame || !playing || emscripten_get_now() - web_last < 2000)
		return;
	web_last = emscripten_get_now();
	write_save(s_save);
	EM_ASM(FS.syncfs(false, function(){}););
}

/* death, quit, win: the character is gone, unless the player saved */
void
port_web_exit(void)
{
	if (!web_saved)
		remove(s_save);
	EM_ASM({ Module.rp.end($0); }, web_saved);
	emscripten_sleep(300);
}
#endif

/* window closed / Cmd-Q: keep the game */
void
port_autosave(void)
{
	if (!fe_ingame || !playing)
		return;
	if (is_saved)
		memcpy(vram, saved_vram, sizeof saved_vram);
	write_save(s_save);
}

void
restore(char *savefile)
{
	FILE *f;
	bool good;

	winit();
	if ((f = fopen(savefile, "rb")) == NULL)
		fatal("%s not found\n", savefile);
	good = port_restore(f);
	fclose(f);
	if (!good)
		fatal("%s is not a Rogue PC save file (or from another version)\n", savefile);
#ifndef __EMSCRIPTEN__	/* web: the file is the autosave, removed on death */
	remove(savefile);	/* as the original: one life */
#endif
	is_saved = FALSE;
	fe_ingame = TRUE;
	explore_reset();
	mpos = 0;
	ifterse1("%s, Welcome back!", "Hello %s, Welcome back to the Dungeons of Doom!", whoami);
	dnum = srand();		/* make it a little tougher on cheaters */
}
