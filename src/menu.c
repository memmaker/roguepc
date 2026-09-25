/*
 * menu.c - floating menus (RVIP 2026)
 *
 *  - Enter: menu of every command, grouped like the help screen
 *  - i:     inventory with a cursor; letter = main action, Shift+letter =
 *           drop, Enter/5/click = menu of all actions for the item
 *  - every "which object?" prompt shows the same list with a cursor
 * Boxes are sized to their content and drawn over the screen (a pop-up in
 * the tiles view). Numpad: 8/2 move, 5/Enter choose, 6 open/confirm,
 * 4 back, + main action, - drop, * examine, 0/. close.
 */

#include "rogue.h"
#include "curses.h"
#include "fe.h"

THING *inv_pick = NULL;		/* item chosen in a menu, taken by get_item() */
bool inv_again = FALSE;		/* reopen the inventory after the action */

#define MAXITEMS_M	64
#define A_BORDER	0x0e	/* yellow */
#define A_TITLE		0x0f
#define A_TEXT		0x07
#define A_KEY		0x0b	/* light cyan */
#define A_CURSOR	0x70	/* reverse */

struct menu {
	const char *title;
	int n;
	const char *label[MAXITEMS_M];
	int key[MAXITEMS_M];		/* key shown in front, 0 = none */
	int cur;
	int row, col;			/* top left; col -1 = centred */
	bool keep;			/* stay on screen after a choice */
	bool own, open;			/* menu_close(): wrestor(), still shown */
	unsigned short under[25][80];
};

static void
menu_close(struct menu *m)
{
	if (!m->open)
		return;
	m->open = FALSE;
	memcpy(vram, m->under, sizeof vram);
	fe_popup_pop();
	if (m->own)
		wrestor();
}

#define M_CANCEL	(-1)
#define M_KEYOF(r)	(-2 - (r))	/* r <= -2: a key the menu didn't use */

static void
put(int r, int c, byte ch, int attr)
{
	if (r >= 0 && r < 25 && c >= 0 && c < 80)
		vram[r][c] = attr << 8 | ch;
}

static void
puts_at(int r, int c, const char *s, int attr, int max)
{
	for (; *s && max > 0; s++, c++, max--)
		put(r, c, *s, attr);
}

static const char *
keyname(int k)
{
	static char buf[8];

	if (k == '\n')
		return "Enter";
	if (k == ESCAPE)
		return "Esc";
	if (k > 0 && k < ' ')
	{
		sprintf(buf, "^%c", k + '@');
		return buf;
	}
	if (k >= FK_F1 && k <= FK_F10)
	{
		sprintf(buf, "F%d", k - FK_F1 + 1);
		return buf;
	}
	if (k == -12)
		return "F12";
	if (k < 0)
		return "";
	buf[0] = k;
	buf[1] = 0;
	return buf;
}

/*
 * Show the menu, return the chosen index, M_CANCEL, or -2-key for a key
 * the caller handles (letters, + - *, ...).
 */
static int
menu_run(struct menu *m, bool (*takes)(int key))
{
	int i, w = 0, h, r0, c0, top = 0, rows, key, ret = M_CANCEL, kw = 0;

	if (m->n == 0)
		return M_CANCEL;
	for (i = 0; i < m->n; i++)
	{
		int l = strlen(m->label[i]);
		if (m->key[i] && (int)strlen(keyname(m->key[i])) > kw)
			kw = strlen(keyname(m->key[i]));
		if (l > w)
			w = l;
	}
	if (kw)
		w += kw + 1;
	if ((int)strlen(m->title) > w)
		w = strlen(m->title);
	if (w > 76)
		w = 76;
	rows = m->n > 21 ? 21 : m->n;
	h = rows + 2;
	r0 = m->row;
	if (r0 + h > 24)
		r0 = 24 - h;
	c0 = m->col < 0 ? (80 - w - 4) / 2 : m->col;
	if (c0 + w + 4 > 80)
		c0 = 80 - w - 4;
	if (m->cur >= m->n)
		m->cur = m->n - 1;
	if (m->cur < 0)
		m->cur = 0;

	if (!m->open)
	{
		m->own = !is_saved;
		if (m->own)
			wdump();
		memcpy(m->under, vram, sizeof vram);
		m->open = TRUE;
		fe_popup_push(r0, c0, r0 + h - 1, c0 + w + 3);
	}
	else
		memcpy(vram, m->under, sizeof vram);
	for (;;)
	{
		if (m->cur < top)
			top = m->cur;
		if (m->cur >= top + rows)
			top = m->cur - rows + 1;
		/* box, 1 space padding inside */
		for (i = 0; i < w + 2; i++)
		{
			put(r0, c0 + 1 + i, HWALL, A_BORDER);
			put(r0 + h - 1, c0 + 1 + i, HWALL, A_BORDER);
		}
		put(r0, c0, ULWALL, A_BORDER);
		put(r0, c0 + w + 3, URWALL, A_BORDER);
		put(r0 + h - 1, c0, LLWALL, A_BORDER);
		put(r0 + h - 1, c0 + w + 3, LRWALL, A_BORDER);
		puts_at(r0, c0 + 2, m->title, A_TITLE, w);
		if (top > 0)
			put(r0, c0 + w + 2, 0x18, A_TITLE);
		if (top + rows < m->n)
			put(r0 + h - 1, c0 + w + 2, 0x19, A_TITLE);
		for (i = 0; i < rows; i++)
		{
			int it = top + i, r = r0 + 1 + i, c = c0 + 2, a;
			a = it == m->cur ? A_CURSOR : A_TEXT;
			put(r, c0, VWALL, A_BORDER);
			put(r, c0 + w + 3, VWALL, A_BORDER);
			for (int j = 0; j < w + 2; j++)
				put(r, c0 + 1 + j, ' ', a);
			if (kw)
			{
				if (m->key[it])
					puts_at(r, c, keyname(m->key[it]), it == m->cur ? A_CURSOR : A_KEY, kw);
				c += kw + 1;
			}
			puts_at(r, c, m->label[it], a, w - (c - c0 - 2));
		}
		cursor(FALSE);
		fe_present();

		key = fe_getkey(-1);
		switch (key)
		{
		case FK_UP: case FK_KP8: case FK_WHEELUP:
			m->cur = (m->cur + m->n - 1) % m->n;
			continue;
		case FK_DOWN: case FK_KP2: case FK_WHEELDOWN:
			m->cur = (m->cur + 1) % m->n;
			continue;
		case FK_PGUP: case FK_KP9:
			m->cur = m->cur > rows ? m->cur - rows : 0;
			continue;
		case FK_PGDN: case FK_KP3:
			m->cur = m->cur + rows < m->n ? m->cur + rows : m->n - 1;
			continue;
		case FK_HOME: case FK_KP7:
			m->cur = 0;
			continue;
		case FK_END: case FK_KP1:
			m->cur = m->n - 1;
			continue;
		case '\n': case FK_KPENTER: case FK_KP5: case FK_KP6: case FK_RIGHT:
			ret = m->cur;
			break;
		case FK_CLICK:
			if (fe_click_row > r0 && fe_click_row < r0 + h - 1
			  && fe_click_col >= c0 && fe_click_col <= c0 + w + 3)
			{
				ret = top + fe_click_row - r0 - 1;
				break;
			}
			ret = M_CANCEL;
			break;
		case ESCAPE: case FK_KP0: case FK_KPDOT: case FK_KP4: case FK_LEFT:
			ret = M_CANCEL;
			break;
		case FK_KPPLUS:
			key = '+';
			goto other;
		case FK_KPMINUS:
			key = '-';
			goto other;
		case FK_KPSTAR:
			key = '*';
			goto other;
		default:
		other:
			if (key < 0)
				continue;
			if (takes && !takes(key))
			{
				for (i = 0; i < m->n; i++)
					if (m->key[i] == key)
						break;
				if (i < m->n)
				{
					ret = i;
					break;
				}
			}
			if (key == '0' || key == '.')
			{
				ret = M_CANCEL;
				break;
			}
			ret = -2 - key;
			break;
		}
		break;
	}
	if (!m->keep || ret == M_CANCEL)
		menu_close(m);
	return ret;
}

/* ---- Enter: all commands ----------------------------------------------- */

struct cmd { int key; const char *desc; };

static const struct cmd c_move[] = {
	{'x', "explore automatically"},
	{'>', "go down the stairs (walks to them)"},
	{'<', "go up the stairs (with the Amulet)"},
	{'s', "search for traps and secret doors"},
	{'.', "rest"},
	{'f', "<dir> find something (run)"},
	{'h', "move left (H: run)"},
	{'j', "move down (J: run)"},
	{'k', "move up (K: run)"},
	{'l', "move right (L: run)"},
	{'y', "move up & left (Y: run)"},
	{'u', "move up & right (U: run)"},
	{'b', "move down & left (B: run)"},
	{'n', "move down & right (N: run)"},
	{0, 0}
};
static const struct cmd c_items[] = {
	{'i', "inventory"},
	{'e', "eat food"},
	{'q', "quaff a potion"},
	{'r', "read a scroll"},
	{'z', "<dir> zap with a wand"},
	{'t', "<dir> throw something"},
	{'w', "wield a weapon"},
	{'W', "wear armor"},
	{'T', "take armor off"},
	{'P', "put on a ring"},
	{'R', "remove a ring"},
	{'d', "drop an object"},
	{'c', "call (name) an object"},
	{0, 0}
};
static const struct cmd c_info[] = {
	{'?', "list of commands"},
	{'/', "list of symbols"},
	{'D', "recall what's been discovered"},
	{'^', "<dir> identify a trap"},
	{CTRL('R'), "repeat last message"},
	{'v', "version"},
	{0, 0}
};
static const struct cmd c_game[] = {
	{'S', "save the game"},
	{'Q', "quit"},
	{'a', "repeat last command"},
	{CTRL('F'), "The Any Key (macro)"},
	{'F', "define the Any Key"},
	{CTRL('T'), "terse messages on/off"},
	{'!', "Supervisor Key (fake DOS)"},
	{-1, "auto_more on/off"},
	{-12, "tiles / text view"},
	{0, 0}
};
static const struct { const char *name; const struct cmd *cmds; } groups[] = {
	{ "Moving", c_move }, { "Items", c_items },
	{ "Information", c_info }, { "Game", c_game },
};

extern void fe_toggle_mode(void);

static bool
takes_nothing(int key)
{
	return FALSE;
}

byte
cmd_menu(void)
{
	static int gcur = 0, ccur[4];
	static struct menu m;
	int g, i, r;

	for (;;)
	{
		memset(&m, 0, sizeof m);
		m.title = "Commands";
		m.row = 1;
		m.col = -1;
		m.cur = gcur;
		for (g = 0; g < 4; g++)
		{
			m.label[g] = groups[g].name;
			m.key[g] = '1' + g;
		}
		m.n = 4;
		if ((r = menu_run(&m, takes_nothing)) < 0)
		{
			/* a command key typed at the group menu runs directly */
			if (r != M_CANCEL && M_KEYOF(r) < 0x100)
				return M_KEYOF(r);
			return ' ';
		}
		gcur = g = r;

		memset(&m, 0, sizeof m);
		m.title = groups[g].name;
		m.row = 1;
		m.col = -1;
		m.cur = ccur[g];
		for (i = 0; groups[g].cmds[i].desc; i++)
		{
			m.label[i] = groups[g].cmds[i].desc;
			m.key[i] = groups[g].cmds[i].key;
		}
		m.n = i;
		r = menu_run(&m, takes_nothing);
		if (r == M_CANCEL)
			continue;
		if (r < 0)
			return M_KEYOF(r) < 0x100 ? M_KEYOF(r) : ' ';
		ccur[g] = r;
		switch (groups[g].cmds[r].key)
		{
		case -1:
			fe_auto_more = !fe_auto_more;
			fe_save_cfg();
			msg("auto_more is %s", fe_auto_more ? "on" : "off");
			return ' ';
		case -12:
			fe_toggle_mode();
			return ' ';
		}
		return groups[g].cmds[r].key;
	}
}

/* ---- inventory ------------------------------------------------------ */

static byte
main_action(THING *obj)
{
	switch (obj->o_type)
	{
	case FOOD:   return 'e';
	case POTION: return 'q';
	case SCROLL: return 'r';
	case STICK:  return 'z';
	case ARMOR:  return obj == cur_armor ? 'T' : 'W';
	case RING:   return (obj == cur_ring[LEFT] || obj == cur_ring[RIGHT]) ? 'R' : 'P';
	case WEAPON:
		if (obj != cur_weapon && !(obj->o_flags & ISMISL))
			return 'w';
		return 't';
	}
	return '*';
}

static void
examine(THING *obj)
{
	msg("%s (%c)", inv_name(obj, FALSE), pack_char(obj));
}

/* menu of every action that fits; returns the command key or 0 */
static byte
item_actions(THING *obj, int row)
{
	static struct menu m;
	static const struct { byte key; const char *name; } acts[] = {
		{'e', "eat"}, {'q', "quaff"}, {'r', "read"}, {'z', "zap with"},
		{'W', "wear"}, {'T', "take off"}, {'P', "put on"}, {'R', "remove"},
		{'w', "wield"}, {'t', "throw"}, {'d', "drop"}, {'c', "call (name)"},
		{'*', "examine"},
	};
	byte keys[16], mk = main_action(obj);
	int i, n = 0, r;
	bool worn = obj == cur_armor || obj == cur_ring[LEFT] || obj == cur_ring[RIGHT];
	char title[80];

	memset(&m, 0, sizeof m);
	for (i = 0; i < (int)(sizeof acts / sizeof *acts); i++)
	{
		byte k = acts[i].key;
		bool ok;
		switch (k)
		{
		case 'e': ok = obj->o_type == FOOD; break;
		case 'q': ok = obj->o_type == POTION; break;
		case 'r': ok = obj->o_type == SCROLL; break;
		case 'z': ok = obj->o_type == STICK; break;
		case 'W': ok = obj->o_type == ARMOR && obj != cur_armor; break;
		case 'T': ok = obj == cur_armor; break;
		case 'P': ok = obj->o_type == RING && !worn; break;
		case 'R': ok = obj->o_type == RING && worn; break;
		case 'w': ok = obj != cur_weapon && !worn; break;
		case 't': ok = !worn; break;
		case 'd': ok = TRUE; break;
		case 'c': ok = obj->o_type == POTION || obj->o_type == SCROLL
				|| obj->o_type == RING || obj->o_type == STICK; break;
		default: ok = TRUE;
		}
		if (!ok)
			continue;
		/* main action first */
		if (k == mk && n > 0)
		{
			memmove(&m.label[1], &m.label[0], n * sizeof m.label[0]);
			memmove(&m.key[1], &m.key[0], n * sizeof m.key[0]);
			memmove(&keys[1], &keys[0], n);
			m.label[0] = acts[i].name;
			m.key[0] = k;
			keys[0] = k;
		}
		else
		{
			m.label[n] = acts[i].name;
			m.key[n] = k;
			keys[n] = k;
		}
		n++;
	}
	snprintf(title, sizeof title, "%c) %.60s", pack_char(obj), inv_name(obj, FALSE));
	m.title = title;
	m.n = n;
	m.row = row;
	m.col = -1;
	r = menu_run(&m, takes_nothing);
	if (r >= 0)
		return keys[r];
	return 0;
}

static bool
takes_letters(int key)
{
	return TRUE;
}

static bool
monster_near(void)
{
	THING *tp;

	for (tp = mlist; tp != NULL; tp = next(tp))
		if (see_monst(tp))
			return TRUE;
	return FALSE;
}

/*
 * 'i': returns the command to run (with inv_pick set) or 0
 */
byte
inv_menu(void)
{
	static int cur = 0;
	static char lines[MAXPACK + 2][MAXSTR + 4];
	static struct menu m;
	THING *obj, *items[MAXPACK + 2];
	int n, r, key;
	byte k;

	for (;;)
	{
		menu_close(&m);
		memset(&m, 0, sizeof m);
		m.keep = TRUE;
		n = 0;
		for (obj = pack; obj != NULL && n < MAXPACK + 1; obj = next(obj))
		{
			snprintf(lines[n], sizeof lines[n], "%.70s", inv_name(obj, FALSE));
			items[n] = obj;
			m.label[n] = lines[n];
			m.key[n] = pack_char(obj);
			n++;
		}
		if (n == 0)
		{
			msg("you are empty handed");
			return 0;
		}
		m.title = "Inventory  (letter: use, Shift: drop, Enter: menu)";
		m.n = n;
		m.cur = cur;
		m.row = 1;
		m.col = 0;
		r = menu_run(&m, takes_letters);
		cur = m.cur;
		if (r == M_CANCEL)
			return 0;
		if (r >= 0)
		{
			k = item_actions(items[r], r < 18 ? 2 + r : 2);
			if (!k)
				continue;
			obj = items[r];
		}
		else
		{
			key = M_KEYOF(r);
			if (key >= 'a' && key < 'a' + n)
			{
				obj = items[key - 'a'];
				k = main_action(obj);
			}
			else if (key >= 'A' && key < 'A' + n)
			{
				obj = items[key - 'A'];
				k = 'd';
			}
			else if (key >= 1 && key <= 26 && key - 1 < n && key != '\b' && key != '\n')
			{
				obj = items[key - 1];
				k = '*';
			}
			else if (key == '+' || key == '-' || key == '*')
			{
				obj = items[m.cur];
				k = key == '+' ? main_action(obj) : key == '-' ? 'd' : '*';
			}
			else
			{
				menu_close(&m);
				return key < 0x100 ? key : 0;	/* a normal command */
			}
		}
		menu_close(&m);
		if (k == '*')
		{
			examine(obj);
			continue;
		}
		inv_pick = obj;
		inv_again = !monster_near();
		return k;
	}
}

/*
 * The cursor list for "which object do you want to ...?"; returns the
 * letter, or ESCAPE
 */
byte
item_prompt(char *purpose, int type)
{
	static char lines[MAXPACK + 2][MAXSTR + 4];
	char title[80];
	static struct menu m;
	THING *obj;
	int n = 0, r, key;
	bool all = FALSE;

	for (;;)
	{
		memset(&m, 0, sizeof m);
		n = 0;
		for (obj = pack; obj != NULL && n < MAXPACK + 1; obj = next(obj))
		{
			if (!all && type && type != obj->o_type && !(type == CALLABLE &&
			  (obj->o_type == SCROLL || obj->o_type == POTION ||
			  obj->o_type == RING || obj->o_type == STICK)) &&
			  !(type == WEAPON && obj->o_type == POTION) &&
			  !(type == STICK && obj->o_enemy && obj->o_charges))
				continue;
			snprintf(lines[n], sizeof lines[n], "%.70s", inv_name(obj, FALSE));
			m.label[n] = lines[n];
			m.key[n] = pack_char(obj);
			n++;
		}
		if (n == 0 && !all)
		{
			all = TRUE;
			continue;
		}
		snprintf(title, sizeof title, "%c%s which object?%s", toupper(*purpose),
			purpose + 1, all ? "" : "  (* all)");
		m.title = title;
		m.n = n;
		m.row = 1;
		m.col = 0;
		r = menu_run(&m, takes_letters);
		if (r == M_CANCEL)
			return ESCAPE;
		if (r >= 0)
			return m.key[r];
		key = M_KEYOF(r);
		if (key == '*' && !all)
		{
			all = TRUE;
			continue;
		}
		if (key == '+')
			return m.key[m.cur];
		if (key >= 'a' && key <= 'z')
			return key;
	}
}

/*
 * Keys the port feeds into com_char(): a command chosen in a menu, the
 * reopened inventory, auto-explore steps. 0 = read the keyboard.
 */
byte pending_key = 0;

byte
port_key(void)
{
	byte k;

	if (pending_key)
	{
		k = pending_key;
		pending_key = 0;
		return k;
	}
	if (inv_again)
	{
		inv_again = FALSE;
		if (!monster_near() && !no_command)
			return 'i';
	}
	return explore_key();
}

/* a key typed at the command prompt */
byte
port_command(byte ch)
{
	if (ch == '\n')
		ch = cmd_menu();
	return explore_begin(ch);
}
