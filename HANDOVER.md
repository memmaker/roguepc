# Rogue PC 1.48 (rogue-pc-modern-C): handover

## Source and changes

- Base: **Rogue PC 1.48 (rogue-pc-modern-C)**
- Original source: https://github.com/memmaker/roguepc/tree/71f524f (untouched import, commit 71f524f (from the archive rogue-pc-modern-C-main.DOS-Version-1.4.8.zip, sha256 64a41e51177ec62c…; its download source was not recorded))
- Our changes: https://github.com/memmaker/roguepc/compare/71f524f...main (memmaker/roguepc)
- Prompt line (`RvipWM.prompt`, RVIP 5.9): `fe_at_cmd` (new global in `src/command.c`, set around `readchar()` in
  `com_char()`), `js_key(fe_at_cmd)` in `port/fe_web.c`; `port/fe_web.c` sends
  the live message row (`be_prompt`).
- Text windows (RVIP part 2, Presentation rules (W0) rule 6): Status, Messages, Inventory and the pop-up are HTML lines sent by `fe_web.c` (`be_line`/`be_rows`/
  `be_cursor`/`be_popup`, message history kept in C); the only canvas is the map: the PC
  screen (F12, WM window `text`, the one-window layout) is 25 HTML lines in the VGA woff
  (`send_screen`, runs "\x05[*]#fg[/#bg]"…"\x06"), the title picture `rogue-title.png`,
  list icons CSS sprites from `tiles-oryx.png` / `tiles-dawn.png`; `port/mkweb.py` makes
  the PNGs and `tiles-web.json` (DawnLike slots, animated slots) at build time.
