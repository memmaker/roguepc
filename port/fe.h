/*
 * fe.h - frontend interface of the Rogue PC port (RVIP 2026)
 */
#ifndef FE_H
#define FE_H

/* keys that are not plain characters (fe_getkey) */
enum {
	FK_UP = 0x100, FK_DOWN, FK_LEFT, FK_RIGHT, FK_HOME, FK_END,
	FK_PGUP, FK_PGDN, FK_INS, FK_DEL,
	FK_KP0, FK_KP1, FK_KP2, FK_KP3, FK_KP4, FK_KP5, FK_KP6, FK_KP7,
	FK_KP8, FK_KP9, FK_KPDOT, FK_KPENTER, FK_KPPLUS, FK_KPMINUS,
	FK_KPSTAR, FK_KPSLASH,
	FK_F1, FK_F2, FK_F3, FK_F4, FK_F5, FK_F6, FK_F7, FK_F8, FK_F9,
	FK_F10, FK_ALTF9,
	FK_CLICK,		/* mouse click on the text grid: fe_click_row/col */
	FK_WHEELUP, FK_WHEELDOWN
};

/* the text screen (pcvideo.c) */
extern unsigned short vram[25][80], saved_vram[25][80], curtain_vram[25][80];
extern int curtain_down, cur_row, cur_col, cur_on;
extern int is_saved;

void	fe_init(void);
void	fe_present(void);
int	fe_getkey(int msdelay);		/* -1 on timeout */
int	fe_kbhit(void);
void	fe_flush(void);
void	fe_beep(void);
void	fe_msg(const char *msg);	/* message history */
void	fe_splash(const char *path);	/* CGA title picture, NULL = off */
void	fe_fatal(const char *text);
extern int fe_click_row, fe_click_col;
extern int fe_auto_more;		/* --More-- doesn't wait */
extern int fe_ingame;			/* a level is on screen */

void	fe_popup_push(int r0, int c0, int r1, int c1);
void	fe_popup_pop(void);
void	fe_toggle_mode(void);
void	fe_save_cfg(void);
extern int fe_msgs;			/* messages so far (explore stops) */

/* game side of the port (src/explore.c, src/menu.c) */
void	explore_reset(void);
void	explore_stop(void);
unsigned char	explore_key(void);
unsigned char	explore_begin(unsigned char ch);
unsigned char	cmd_menu(void);
unsigned char	inv_menu(void);
unsigned char	item_prompt(char *purpose, int type);
unsigned char	port_key(void);
unsigned char	port_command(unsigned char ch);
extern unsigned char pending_key;
extern _Bool inv_again;
#ifdef next
extern THING *inv_pick;
#endif

/* tiles.c (knows the game data) */
int	tile_for(int y, int x, unsigned char ch, int *under);
int	obj_color(int type);
int	inv_lines(char lines[][81], unsigned char attrs[], int max);

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
void	port_web_autosave(void);
void	port_web_exit(void);
#endif

#endif
