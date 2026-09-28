# Rogue PC 1.48 (rogue-pc-modern-C): handover

## Source and changes

- Base: **Rogue PC 1.48 (rogue-pc-modern-C)**
- Original source: https://github.com/memmaker/roguepc/tree/71f524f (untouched import, commit 71f524f (from the archive rogue-pc-modern-C-main.DOS-Version-1.4.8.zip, sha256 64a41e51177ec62c…; its download source was not recorded))
- Our changes: https://github.com/memmaker/roguepc/compare/71f524f...main (memmaker/roguepc)
- Prompt line (RVIP step 5 / W4, 2026-09-26): the live message row is shown in a
  box over the map by `RvipWM.prompt` (rvip-wm.js). A key hides it only while
  the game waits for a command, so a question stays up until answered.
  Here: `fe_at_cmd` (new global in `src/command.c`, set around `readchar()` in
  `com_char()`), `js_key(fe_at_cmd)` in `port/fe_web.c`; `port/fe_web.c` sends
  the live message row (`be_prompt`). Text windows (W0 rule 6, 2026-09-28): Status,
  Messages, Inventory and the pop-up are HTML lines sent by `fe_web.c` (`be_line`/`be_rows`/
  `be_cursor`/`be_popup`, message history kept in C); the only canvases are the map and
  the text-mode VGA screen.
