/*
 * Rogue PC in the browser: draws the frames port/fe_web.c sends (Module.rp).
 * Tiles mode: tiling windows (map, messages, status, inventory, pop-up over
 * the map) like the other web ports. Text mode: one window with the original
 * IBM text screen (VGA 9x16 font, CP437, CGA colours, blink). Keyboard,
 * mouse, saves in IndexedDB. Loaded before roguepc-core.js.
 */
(function () {
	'use strict';

	var DIR = RvipApp.dir, SAVE = DIR + '/rogue.sav', LAYOUT_FILE = DIR + '/web-layout.json';
	var FONT = '"DejaVu Sans Mono", Menlo, Consolas, "Liberation Mono", monospace';
	var GUT = 6, TITLE_H = 20, BORDER = 2;
	var TILE_STEPS = [12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64];
	var PAL = ['#000000', '#0000aa', '#00aa00', '#00aaaa', '#aa0000', '#aa00aa', '#aa5500', '#aaaaaa',
		'#555555', '#5555ff', '#55ff55', '#55ffff', '#ff5555', '#ff55ff', '#ffff55', '#ffffff'];
	var CGA1 = [[0, 0, 0], [0x55, 0xff, 0xff], [0xff, 0x55, 0xff], [0xff, 0xff, 0xff]];
	/* CP437 -> Unicode, for the text windows */
	var CP437 = ' ☺☻♥♦♣♠•◘○◙♂♀♪♫☼►◄↕‼¶§▬↨↑↓→←∟↔▲▼' +
		' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~⌂' +
		'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀' +
		'αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
	/* port/fe.h FK_* */
	var FK = { ArrowUp: 0x100, ArrowDown: 0x101, ArrowLeft: 0x102, ArrowRight: 0x103, Home: 0x104, End: 0x105,
		PageUp: 0x106, PageDown: 0x107, Insert: 0x108, Delete: 0x109 };
	var FK_KP0 = 0x10a, FK_KPDOT = 0x114, FK_KPENTER = 0x115, FK_KPPLUS = 0x116, FK_KPMINUS = 0x117,
		FK_KPSTAR = 0x118, FK_KPSLASH = 0x119, FK_F1 = 0x11a, FK_ALTF9 = 0x124, FK_CLICK = 0x125;

	var events = [], clickAt = 0, atCmd = 0, app;
	var dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
	var L = null, rects = {};
	var fontSheets = [], tileSheets = [], tileCol = null;
	var TW = 16, TH = 24;              /* ClassicRogue sprites (tiles by Oryx), 1 bit */
	var kind = 'text';                 /* what C sent last: 'text' or 'tiles' */
	var F = null;                      /* last tiles frame */
	var T = null;                      /* last text frame */
	var hero = { y: 0, x: 0 }, off = { x: 0, y: 0 }, lastLevel = -1;
	var mapCv, mapCtx, mapPrev = null;

	function $(id) { return document.getElementById(id); }
	function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
	function status(msg, isError) { app.status(msg, isError); }
	function blinkOn() { return ((performance.now() / 229) | 0) & 1; }
	function curOn() { return ((performance.now() / 115) | 0) & 1; }

	/* ---------- graphics from the game's own data ---------- */
	function buildSheets(fontPtr, tilesPtr, colPtr, ntiles) {
		var H16 = Module.HEAPU16, c, x, y, g;
		var mask = new Uint8Array(144 * 256);
		for (g = 0; g < 256; g++)
			for (y = 0; y < 16; y++) {
				var row = H16[(fontPtr >> 1) + g * 16 + y];
				for (x = 0; x < 9; x++)
					if ((row >> (8 - x)) & 1) mask[((g >> 4) * 16 + y) * 144 + (g & 15) * 9 + x] = 1;
			}
		for (c = 0; c < 16; c++) {
			var cv = document.createElement('canvas');
			cv.width = 144; cv.height = 256;
			var ctx = cv.getContext('2d'), im = ctx.createImageData(144, 256);
			var r = parseInt(PAL[c].substr(1, 2), 16), gg = parseInt(PAL[c].substr(3, 2), 16), b = parseInt(PAL[c].substr(5, 2), 16);
			for (var i = 0; i < mask.length; i++)
				if (mask[i]) { im.data[i * 4] = r; im.data[i * 4 + 1] = gg; im.data[i * 4 + 2] = b; im.data[i * 4 + 3] = 255; }
			ctx.putImageData(im, 0, 0);
			fontSheets[c] = cv;
		}
		/* sprites: one sheet per CGA colour, 32 per row */
		var rows = Math.ceil(ntiles / 32), sw = 32 * TW, sh = rows * TH;
		tileCol = Module.HEAPU8.slice(colPtr, colPtr + ntiles);
		for (c = 0; c < 16; c++) {
			var tc = document.createElement('canvas');
			tc.width = sw; tc.height = sh;
			var tctx = tc.getContext('2d'), ti = tctx.createImageData(sw, sh);
			var q = [1, 3, 5].map(function (k) { return parseInt(PAL[c].substr(k, 2), 16); });
			for (g = 0; g < ntiles; g++)
				for (y = 0; y < TH; y++) {
					var bits = H16[(tilesPtr >> 1) + g * TH + y];
					for (x = 0; x < TW; x++)
						if ((bits >> (15 - x)) & 1) {
							var o = (((g >> 5) * TH + y) * sw + (g & 31) * TW + x) * 4;
							ti.data[o] = q[0]; ti.data[o + 1] = q[1]; ti.data[o + 2] = q[2]; ti.data[o + 3] = 255;
						}
				}
			tctx.putImageData(ti, 0, 0);
			tileSheets[c] = tc;
		}
	}

	/* second tile set: DawnLike in full colour, 16x16, same sprite numbers (port/mkdawn.py);
	   covers every slot the map uses; never mixed with Oryx. DawnLike|a animates
	   the map with DawnLike's second frame. Last: None (the map in text). The
	   choice is kept in the layout file (IndexedDB) */
	var TILESETS = ['oryx', 'dawn', 'dawna', 'none'], TSNAME = { oryx: 'Oryx', dawn: 'DawnLike', dawna: 'DawnLike|a', none: 'None' };
	var dawn = new Image(), dawn1 = new Image(), dawnHas = [], tset = 'oryx', useDawn = false, frame = 0, anim = null, onMap = false;
	dawn.onload = function () {
		var cv = document.createElement('canvas'); cv.width = dawn.width; cv.height = dawn.height;
		var cx = cv.getContext('2d'); cx.drawImage(dawn, 0, 0);
		var d = cx.getImageData(0, 0, cv.width, cv.height).data;
		for (var t = 0; t < (cv.width / 16) * (cv.height / 16); t++)
			dawnHas[t] = d[(((t >> 5) * 16 + 8) * cv.width + (t & 31) * 16 + 8) * 4 + 3] > 0;
		if (useDawn && L) tilesetChanged();
	};
	dawn.src = 'tiles-dawn.png';
	dawn1.src = 'tiles-dawn-1.png';
	function setTileset(t) { tset = TILESETS.indexOf(t) >= 0 ? t : 'oryx'; useDawn = tset === 'dawn' || tset === 'dawna'; frame = 0; }
	/* DawnLike|a: twice a second the map draws from the frame-1 sheet; only the
	   cells whose sprite (or the floor under it) differs between the frames */
	function findAnim() {
		var px = function (im) { var c = document.createElement('canvas'); c.width = im.naturalWidth; c.height = im.naturalHeight; var x = c.getContext('2d'); x.drawImage(im, 0, 0); return x.getImageData(0, 0, c.width, c.height).data; };
		var a = px(dawn), b = px(dawn1), W = dawn.naturalWidth, n = (W / 16) * (dawn.naturalHeight / 16);
		anim = new Uint8Array(n);
		for (var t = 0; t < n; t++)
			for (var y = 0, x0 = (t & 31) * 16, y0 = (t >> 5) * 16; y < 16 && !anim[t]; y++)
				for (var i = ((y0 + y) * W + x0) * 4, e = i + 64; i < e; i++) if (a[i] !== b[i]) { anim[t] = 1; break; }
	}
	setInterval(function () {
		if (tset !== 'dawna' || !dawnHas.length || !dawn1.naturalWidth || document.hidden || !mapPrev) return;
		if (!anim) findAnim();
		frame ^= 1;
		for (var i = 0; i < 22 * 80; i++) if (anim[mapPrev.t[i]] || anim[mapPrev.u[i]]) mapPrev.t[i] = -99;   /* stale: redraw */
		drawMap();
	}, 500);
	function dawnOn() { return useDawn && dawnHas.length > 0; }
	function noTiles() { return tset === 'none'; }
	function renderTileset() { $('btn-tileset').textContent = 'Tiles: ' + TSNAME[tset]; }
	/* both lists follow right away: Visible from its cached string, the
	   Inventory (its rows are the game's) by a ^L at the command prompt */
	function tilesetChanged() {
		var vb = $('vis');
		if (vb._vis != null) { var v = vb._vis; vb._vis = null; RvipWM.visible(vb, v, visIcon); }
		if (txt[P_INV]) for (var y = 0; y < txt[P_INV].n; y++) drawRow(P_INV, y);
		if (atCmd && app.running) events.push(12);
		applyDom();
	}
	function toggleTileset() {
		tset = TILESETS[(TILESETS.indexOf(tset) + 1) % TILESETS.length];
		setTileset(tset);
		L.tiles = tset; saveLayout();
		renderTileset(); renderMapSel(); tilesetChanged();
	}
	/* map font chooser: on the Map title bar (shown on hover), with Tiles: None only */
	var mapSel = document.createElement('select');
	mapSel.title = 'Map font (Tiles: None)';
	mapSel.innerHTML = '<option value="">Default font</option>';
	mapSel.addEventListener('pointerdown', function (e) { e.stopPropagation(); });   /* not a window drag */
	mapSel.addEventListener('mousedown', function (e) { e.stopPropagation(); });
	function renderMapSel() {
		var bs = document.querySelector('#t-map .wm-btns');
		if (bs && mapSel.parentNode !== bs) bs.insertBefore(mapSel, bs.firstChild);
		mapSel.hidden = !noTiles();
		mapSel.value = (L && L.mapFace) || '';
	}
	/* a font from the index page's fonts/ (web/build.sh lists them in fonts.json) */
	function loadFace(n, now) {
		var redraw = function () { if (L && wm) { applyFace(); applyDom(); } };
		if (!n) { if (now) redraw(); return; }
		var ff = new FontFace(n, 'url(../fonts/' + n + '.woff)');
		ff.load().then(function () { document.fonts.add(ff); redraw(); }).catch(function () { status('Could not load the font ' + n + '.', true); });
	}
	/* list icon (Inventory, Visible): the sprite as a CSS sprite in a 16px box,
	   at its own aspect ratio; null: no icon (the glyph is shown) */
	var sheetUrl = [];
	function visIcon(t) {
		if (noTiles() || !(t >= 0) || !tileCol) return null;
		var box = document.createElement('span'), e = document.createElement('span'), st = e.style, k;
		box.className = 'wm-ic'; box.style.display = 'flex'; box.style.justifyContent = 'center';
		st.height = '16px'; st.imageRendering = 'pixelated'; st.backgroundRepeat = 'no-repeat';
		if (dawnOn() && dawnHas[t]) {
			st.width = '16px'; st.backgroundImage = 'url(tiles-dawn.png)';
			st.backgroundSize = dawn.naturalWidth + 'px ' + dawn.naturalHeight + 'px';
			st.backgroundPosition = -(t & 31) * 16 + 'px ' + -(t >> 5) * 16 + 'px';
		} else {
			var c = tileCol[t], sh = tileSheets[c];
			if (!sheetUrl[c]) sheetUrl[c] = sh.toDataURL();
			k = 16 / TH;
			st.width = TW * k + 'px'; st.backgroundImage = 'url(' + sheetUrl[c] + ')';
			st.backgroundSize = sh.width * k + 'px ' + sh.height * k + 'px';
			st.backgroundPosition = -(t & 31) * TW * k + 'px ' + -(t >> 5) * TH * k + 'px';
		}
		box.appendChild(e);
		return box;
	}
	/* sprite t in CGA colour c, cell w x h */
	function tile(ctx, t, c, x, y, w, h) {
		if (dawnOn() && dawnHas[t]) ctx.drawImage(onMap && frame && dawn1.naturalWidth ? dawn1 : dawn, (t & 31) * 16, (t >> 5) * 16, 16, 16, x, y, w, h);
		else ctx.drawImage(tileSheets[c], (t & 31) * TW, (t >> 5) * TH, TW, TH, x, y, w, h);
	}
	function cellH() { return noTiles() ? mapTxt().h : dawnOn() ? L.tile : L.tile * TH / TW; }
	/* Tiles: None: the map font sized so one character is a cell (L.tile) wide,
	   rows as tall as the font's own line */
	function mapTxt() {
		var c = document.createElement('canvas').getContext('2d'), m;
		c.font = '100px ' + face('map'); m = c.measureText('M');
		var f = Math.round(100 * L.tile / m.width);
		return { f: f, h: Math.round(f * ((m.fontBoundingBoxAscent + m.fontBoundingBoxDescent) / 100 || 1.2)) };
	}

	/* ---------- text mode: one window, 80x25 VGA cells ---------- */
	var textCv, textCtx;
	function drawText() {
		if (!T || !textCtx) return;
		var c = textCtx, r, col, v, a, s = T.scr;
		c.setTransform(2, 0, 0, 2, 0, 0);
		c.imageSmoothingEnabled = false;
		c.fillStyle = '#000'; c.fillRect(0, 0, 720, 400);
		if (T.pic) {
			var im = c.createImageData(320, 200);
			for (var i = 0; i < 64000; i++) {
				var q = CGA1[T.pic[i]];
				im.data[i * 4] = q[0]; im.data[i * 4 + 1] = q[1]; im.data[i * 4 + 2] = q[2]; im.data[i * 4 + 3] = 255;
			}
			var tmp = document.createElement('canvas'); tmp.width = 320; tmp.height = 200;
			tmp.getContext('2d').putImageData(im, 0, 0);
			c.drawImage(tmp, 40, 0, 640, 400);
			return;
		}
		var bl = blinkOn();
		for (r = 0; r < 25; r++)
			for (col = 0; col < 80; col++) {
				v = s[r * 80 + col]; a = v >> 8;
				if ((a >> 4) & 7) { c.fillStyle = PAL[(a >> 4) & 7]; c.fillRect(col * 9, r * 16, 9, 16); }
				var g = v & 255;
				if (g && g !== 32 && (!(a & 0x80) || bl))
					c.drawImage(fontSheets[a & 15], (g & 15) * 9, (g >> 4) * 16, 9, 16, col * 9, r * 16, 9, 16);
			}
		if (T.con && curOn()) {
			a = s[T.cr * 80 + T.cc] >> 8;
			c.fillStyle = PAL[(a & 15) || 7];
			c.fillRect(T.cc * 9, T.cr * 16 + 13, 9, 2);
		}
	}
	function fitText() {
		var A = areaSize(), sc = Math.min(A.w / 720, A.h / 400);
		textCv.style.width = Math.floor(720 * sc) + 'px';
		textCv.style.height = Math.floor(400 * sc) + 'px';
	}

	/* ---------- tiles mode: text windows (RVIP W0 rule 6) ----------
	 * HTML lines from the game (port/fe_web.c): each changed row once, trimmed,
	 * standout between \x01 and \x02, another colour between "\x05#rrggbb" and
	 * \x06, the row's colour and icon tile, and the rows in use. The WM sets
	 * the text size (A−/A+); the pop-up follows Messages. */
	/* the chosen face for a pane: the map has its own (text mode only) */
	function face(id) { var n = L && (id === 'map' ? L.mapFace : L.face); return n ? '"' + n + '", ' + FONT : FONT; }
	function cp(g) { return CP437.charAt(g); }
	var P_STAT = 0, P_MSG = 1, P_INV = 2, P_POP = 3;
	var PANE_EL = ['#t-stat .body pre', '#t-msg .body pre', '#t-inv .body', '#pop pre'];
	var txt = [], cur = { p: -1, y: 0, x: 0 }, follow = null, popCol = 0, popRow = 0;
	function textPane(p) {
		var el = document.querySelector(PANE_EL[p]);
		el.textContent = '';
		txt[p] = { el: el, lines: [], css: [], tile: [], n: 0 };
	}
	function esc(s) { return s.replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
	function rowHtml(p, y) {
		var T = txt[p], s = T.lines[y] || '', cx = cur.p === p && cur.y === y ? cur.x : -1;
		if (cx >= 0) {                              /* the cursor: one cell, past the end if need be */
			var vis = s.replace(/\x05#[0-9a-f]{6}|[\x01\x02\x06]/g, '');
			while (vis.length <= cx) { s += ' '; vis += ' '; }
			for (var i = 0, k = 0; i < s.length; i++) {
				if (s[i] === '\x05') { i += 7; continue; }
				if (s[i] > '\x06' && k++ === cx) break;
			}
			s = s.slice(0, i) + '\x03' + s[i] + '\x04' + s.slice(i + 1);
		}
		return esc(s).replace(/\x01/g, '<span class="so">').replace(/[\x02\x04\x06]/g, '</span>')
			.replace(/\x03/g, '<span class="cur">').replace(/\x05(#[0-9a-f]{6})/g, '<span style="color:$1">');
	}
	function drawRow(p, y) {
		var T = txt[p], d = T && T.el.children[y];
		if (!d) return;
		d.innerHTML = rowHtml(p, y);
		d.style.color = T.css[y] || '';
		if (p === P_INV) { var ic = visIcon(T.tile[y]); if (ic) d.insertBefore(ic, d.firstChild); }
	}
	function setRows(p, n) {
		var T = txt[p];
		while (T.el.children.length < n) { T.el.appendChild(document.createElement('div')); drawRow(p, T.el.children.length - 1); }
		while (T.el.children.length > n) T.el.removeChild(T.el.lastChild);
		T.n = n;
	}
	function msgMark() {                          /* before a Messages change: was it at the end? */
		var b = txt[P_MSG] && txt[P_MSG].el.parentNode;
		if (b && follow == null) follow = b.scrollTop + b.clientHeight >= b.scrollHeight - 4;
	}
	function popFont() { $('pop').style.fontSize = RvipWM.fontSize('msg') + 'px'; placePop(); }
	/* in the map body (never over its title bar), at the original's column */
	function placePop() { if (!$('pop').hidden && rects.map && L) RvipWM.popup($('pop'), { x: Math.max(0, popCol * L.tile - Math.max(0, off.x)) }); }
	/* the top-bar font on every text window and the pop-up */
	function applyFace() {
		['#t-stat .body', '#t-msg .body', '#t-inv .body', '#t-vis .body', '#pop'].forEach(function (q) { var e = document.querySelector(q); if (e) e.style.fontFamily = face('txt'); });
	}

	/* ---------- tiles mode: the map ---------- */
	function shapeMap() {
		var s = L.tile, h = cellH();
		mapCv.width = Math.round(80 * s * dpr); mapCv.height = Math.round(22 * h * dpr);
		mapCv.style.width = 80 * s + 'px'; mapCv.style.height = 22 * h + 'px';
		mapCtx = mapCv.getContext('2d');
		mapCtx.setTransform(dpr, 0, 0, dpr, 0, 0);
		mapCtx.imageSmoothingEnabled = false;
		mapCtx.fillStyle = '#000'; mapCtx.fillRect(0, 0, 80 * s, 22 * h);
		mapPrev = null;
	}
	function drawMap() {
		if (!F) return;
		var s = L.tile, h = cellH(), c = mapCtx, i, r, col;
		c.font = (L.mapFace ? '' : 'bold ') + (noTiles() ? mapTxt().f : Math.round(s * 0.9)) + 'px ' + face('map');   /* bitmap fonts: not bold */
		c.textAlign = 'center'; c.textBaseline = 'middle';
		onMap = true;   /* tile(): the animation frame applies to the map only */
		for (i = 0; i < 22 * 80; i++) {
			var v = F.scr[80 + i], t = F.t[i], u = F.u[i];
			if (noTiles() && t !== -2) { t = -1; u = -1; }
			if (mapPrev && mapPrev.v[i] === v && mapPrev.t[i] === t && mapPrev.u[i] === u) continue;
			r = (i / 80) | 0; col = i % 80;
			var x = col * s, y = r * h, a = v >> 8;
			c.fillStyle = '#000'; c.fillRect(x, y, s, h);
			if (t === -2) continue;
			if (u >= 0) tile(c, u, tileCol[u], x, y, s, h);
			/* the sprite in the cell's text-mode colour (stairs: black on green) */
			if (t >= 0) tile(c, t, (a & 15) || ((a >> 4) & 7), x, y, s, h);
			else {
				var g = v & 255;
				if ((a >> 4) & 7) { c.fillStyle = PAL[(a >> 4) & 7]; c.fillRect(x, y, s, h); }
				if (g && g !== 32) { c.fillStyle = PAL[a & 15]; c.fillText(cp(g), x + s / 2, y + h / 2 + 1); }
			}
		}
		onMap = false;
		mapPrev = { v: F.scr.slice(80, 80 + 22 * 80), t: F.t.slice(), u: F.u.slice() };
	}
	/* keep the hero in the middle half of the map window */
	function scrollMap() {
		var r = rects.map;
		if (!r) return;
		var ch = cellH();
		off = RvipWM.center(mapCv, (hero.x + 0.5) * L.tile, (hero.y - 0.5) * ch, 80 * L.tile, 22 * ch,
			r[2] - BORDER, r[3] - BORDER - ($('game').classList.contains('wm-single') ? 0 : TITLE_H));
	}

	function drawTiles(full) {
		if (full) shapeMap();
		drawMap();
	}

	/* ---------- layout ---------- */
	/*
	 *   +---------------------------+   bottom: y of map | lower part
	 *   |            map            |   side:   x of left | inventory
	 *   +-------------+-------------+   stat:   y of messages | status
	 *   |  messages   |             |
	 *   +-------------+  inventory  |
	 *   |  status     |             |
	 *   +-------------+-------------+
	 */
	var WINS = ['map', 'msg', 'stat', 'inv', 'vis'], wm = null;

	function areaSize() {
		var g = $('game');
		return { w: g.clientWidth, h: g.clientHeight };
	}
	function defaultLayout() {
		var A = areaSize(), W = A.w, H = A.h;
		if (W < 400 || H < 300) { W = 1280; H = 720; }
		var font = 13, tile = TILE_STEPS[0];
		TILE_STEPS.forEach(function (t) { if (80 * t + BORDER <= W && 22 * t * TH / TW + BORDER <= H * 0.66) tile = t; });
		var mapH = 22 * tile * TH / TW + BORDER, lower = H - mapH - GUT;
		var statH = TITLE_H + BORDER + 2 * Math.round(font * 1.3) + 4;
		return { v: 1, tile: tile, auto: true, mapFace: 'WebPlus_IBM_VGA_9x16',
			split: { bottom: (mapH + GUT / 2) / H, side: 0.5, stat: clamp((lower - statH - GUT / 2) / lower, 0.3, 0.95) }, wm: null };
	}
	function loadLayout() {
		var d = defaultLayout();
		try {
			var s = JSON.parse(Module.FS.readFile(LAYOUT_FILE, { encoding: 'utf8' }));
			if (s && s.v === 1) {
				if (!s.auto) {
					d.auto = false;
					if (TILE_STEPS.indexOf(s.tile) >= 0) d.tile = s.tile;
				}
				if (s.wm) d.wm = s.wm;
				if (s.font && d.wm && !d.wm.fs) { d.wm.fs = {}; ['msg', 'stat', 'inv', 'vis'].forEach(function (k) { if (s.font[k]) d.wm.fs[k] = s.font[k]; }); }   /* old layout: sizes move to the WM */
				if (typeof s.face === 'string') d.face = s.face;
				if (typeof s.mapFace === 'string') d.mapFace = s.mapFace;
				if (typeof s.tiles === 'string') d.tiles = s.tiles;   /* the tile set */
			}
		} catch (err) { /* nothing saved yet */ }
		L = d;
		$('sel-font').value = L.face || '';
		loadFace(L.face); loadFace(L.mapFace);
	}
	var saveTimer = 0;
	function saveLayout() {
		clearTimeout(saveTimer);
		saveTimer = setTimeout(function () {
			try { Module.FS.writeFile(LAYOUT_FILE, JSON.stringify(L)); app.sync(); }
			catch (err) { console.warn('layout not saved', err); }
		}, 400);
	}
	function place(el, r) {
		el.style.left = r[0] + 'px'; el.style.top = r[1] + 'px';
		el.style.width = Math.max(0, r[2]) + 'px'; el.style.height = Math.max(0, r[3]) + 'px';
	}
	function showText() { return kind === 'text' || (wm && wm.mode() === 'single'); }

	/* place the windows for the current mode and redraw everything */
	function applyDom() {
		if (!L) return;
		if (!wm) makeWM();
		$('game').classList.toggle('txt', showText());
		wm.apply();
	}
	/* windows: the shared tiling window manager (rvip-wm.js, RVIP.md 5b);
	 * text mode (the whole 80x25 screen) hides them for #t-text */
	function makeWM() {
		var d = defaultLayout().split, A = areaSize();
		var line = Math.round(13 * 1.3) + 4, stat = 2 * Math.round(13 * 1.3) + 4;
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'),
			wins: [{ id: 'map', title: 'Map' }, { id: 'msg', title: 'Messages' }, { id: 'stat', title: 'Status' }, { id: 'inv', title: 'Inventory' }, { id: 'vis', title: 'Visible' }],
			multi: { d: 'v', r: d.bottom, a: 'map', b: { d: 'h', r: 0.4, a: { d: 'v', r: d.stat, a: 'msg', b: 'stat' }, b: { d: 'h', r: 0.5, a: 'inv', b: 'vis' } } },
			single: { d: 'v', r: line / A.h, a: 'msg', b: { d: 'v', r: 1 - stat / (A.h - line), a: 'map', b: 'stat' } },
			state: L.wm,
			save: function (st) { L.wm = st; saveLayout(); },
			layout: function (r) {
				var A = areaSize(), txt = showText();
				rects = r; rects.text = [0, 0, A.w, A.h];
				$('t-text').hidden = !txt;
				place($('t-text'), rects.text);
				renderMapSel();
				if (txt) { $('pop').style.visibility = 'hidden'; fitText(); drawText(); }
				else { $('pop').style.visibility = ''; drawTiles(true); scrollMap(true); placePop(); }
			},
			/* A- / A+: the map steps its tiles; the text windows are the WM's; the pop-up follows Messages */
			zoom: { map: function (s, d) { zoomMap(d); }, msg: popFont },
			onReset: resetLayout
		});
	}

	function zoomMap(d) {
		var i = clamp(TILE_STEPS.indexOf(L.tile) + d, 0, TILE_STEPS.length - 1);
		L.tile = TILE_STEPS[i]; L.auto = false;
		applyDom(); saveLayout();
		status('Map tiles: ' + L.tile + ' px');
		setTimeout(function () { status(''); }, 1200);
	}
	function resetLayout() {
		var fc = L.face, mf = L.mapFace, ts = L.tiles;
		L = defaultLayout(); L.face = fc; L.mapFace = mf; L.tiles = ts; L.wm = wm.state();
		applyDom(); popFont(); saveLayout();
	}

	/* ---------- called by the game (port/fe_web.c) ---------- */
	var rp = {
		init: function (font, tiles, cols, ntiles, am) {
			buildSheets(font, tiles, cols, ntiles);
			if (!L) loadLayout();
			[P_STAT, P_MSG, P_INV, P_POP].forEach(textPane);
			$('game').hidden = false;
			applyDom(); applyFace(); popFont();
		},
		text: function (scr, cr, cc, con, pic) {
			T = { scr: Module.HEAPU16.slice(scr >> 1, (scr >> 1) + 2000), cr: cr, cc: cc, con: con,
				pic: pic ? Module.HEAPU8.slice(pic, pic + 64000) : null };
			if (kind !== 'text') { kind = 'text'; applyDom(); }
			else drawText();
		},
		tiles: function (scr, vr, t, u, hy, hx, lvl) {
			F = { scr: Module.HEAPU16.slice(scr >> 1, (scr >> 1) + 2000), vr: Module.HEAPU16.slice(vr >> 1, (vr >> 1) + 2000),
				t: Module.HEAP32.slice(t >> 2, (t >> 2) + 1760), u: Module.HEAP32.slice(u >> 2, (u >> 2) + 1760) };
			T = { scr: F.vr, cr: -1, cc: 0, con: 0, pic: null };
			var mb = txt[P_MSG] && txt[P_MSG].el.parentNode;   /* follow the newest message unless scrolled up */
			if (mb && follow) mb.scrollTop = mb.scrollHeight;
			follow = null;
			var moved = hy !== hero.y || hx !== hero.x;
			hero.y = hy; hero.x = hx;
			if (kind !== 'tiles') { kind = 'tiles'; lastLevel = lvl; applyDom(); return; }
			if (showText()) { drawText(); return; }
			drawMap();
			if (moved || lvl !== lastLevel) scrollMap(lvl !== lastLevel);
			placePop();
			lastLevel = lvl;
		},
		line: function (p, y, s, c, t) {
			var X = txt[p];
			if (!X) return;
			if (p === P_MSG) msgMark();
			X.lines[y] = s; X.css[y] = c; X.tile[y] = t;
			if (y < X.n) drawRow(p, y);
		},
		rows: function (p, n) { if (!txt[p]) return; if (p === P_MSG) msgMark(); setRows(p, n); },
		cursor: function (p, y, x) {
			var o = cur.p, oy = cur.y;
			cur.p = p; cur.y = y; cur.x = x;
			if (txt[o]) drawRow(o, oy);
			if (txt[p]) drawRow(p, y);
		},
		popup: function (on, row, col) {
			textPane(P_POP);
			popRow = row; popCol = col;
			$('pop').hidden = !on;
			placePop();
		},
		vis: function (s) {
			RvipWM.visible($('vis'), s.replace(/^([MI])([0-9a-f]{2})/gm, function (m, k, h) { return k + cp(parseInt(h, 16)); })
				.replace(/\t(\d+)\t(-?\d+)$/gm, function (m, c, t) { return '\t' + PAL[+c] + '\t' + t; }), visIcon);
		},
		toggle: function () { if (wm) wm.mode(wm.mode() === 'single' ? 'multi' : 'single'); },
		icons: function () { return noTiles() ? 0 : 1; },
		key: function (a) { atCmd = a; RvipWM.prompt.wait(a); return events.length ? events.shift() : -1; },
		click: function () { return clickAt; },
		pending: function () { return events.length ? 1 : 0; },
		flush: function () { events.length = 0; },
		sync: function () { app.sync(); },
		end: function (saved) {
			app.running = false;
			app.sync(function () {
				$('overlay-msg').textContent = saved ? 'Your game has been saved. Play again to continue it.' : 'The game is over.';
				$('overlay').hidden = false;
			});
		}
	};

	/* blinking attribute and cursor */
	setInterval(function () {
		if (!app.running || !L) return;
		if (showText()) drawText();
	}, 115);

	/* ---------- input ---------- */
	function onKey(e) {
		if (!app.running || e.isComposing || e.metaKey) return;
		var k = e.key, code = e.code || '', m = /^Numpad(\d)$/.exec(code), c;
		if (k === 'F12') { rp.toggle(); e.preventDefault(); return; }
		if (m) c = FK_KP0 + +m[1];
		else if (code === 'NumpadEnter') c = FK_KPENTER;
		else if (code === 'NumpadDecimal') c = FK_KPDOT;
		else if (code === 'NumpadAdd') c = FK_KPPLUS;
		else if (code === 'NumpadSubtract') c = FK_KPMINUS;
		else if (code === 'NumpadMultiply') c = FK_KPSTAR;
		else if (code === 'NumpadDivide') c = FK_KPSLASH;
		else if (k === 'Enter') c = 10;
		else if (k === 'Escape') c = 27;
		else if (k === 'Backspace') c = 8;
		else if (FK[k]) c = FK[k];
		else if (/^F([1-9]|10)$/.test(k)) c = k === 'F9' && e.altKey ? FK_ALTF9 : FK_F1 + (+k.substr(1)) - 1;
		else if (k.length === 1) {
			c = k.charCodeAt(0);
			if (e.ctrlKey && !e.altKey) {
				var u = k.toUpperCase().charCodeAt(0);
				if (u >= 65 && u <= 90) c = u & 0x1f; else return;
			}
			if (c > 126) return;
		}
		else return;
		events.push(c);
		e.preventDefault();
	}
	/* a click on a text cell (row, col of the 80x25 screen) */
	function click(r, c) {
		if (!app.running || r < 0 || r > 24 || c < 0 || c > 79) return;
		clickAt = (r << 8) | c;
		events.push(FK_CLICK);
	}
	function canvasXY(cv, e) {
		var b = cv.getBoundingClientRect();
		return { x: e.clientX - b.left, y: e.clientY - b.top, w: b.width, h: b.height };
	}

	/* ---------- saves: IndexedDB (IDBFS), help (../rvip-app.js) ---------- */
	function hasSave() { try { Module.FS.stat(SAVE); return true; } catch (e) { return false; } }
	app = RvipApp({
		name: 'roguepc',
		save: function () { return hasSave() ? SAVE : null; },
		clear: function () { if (hasSave()) Module.FS.unlink(SAVE); },
		put: function (file, data) { Module.FS.writeFile(SAVE, data); },
		exportName: function () { return 'rogue.sav'; },
		helpText: 'Press F1 in the game for its own help.'
	});

	/* ---------- startup ---------- */
	window.Module = {
		rp: rp,
		arguments: [],
		preRun: [function () {
			var FS = Module.FS;
			Module.addRunDependency('idbfs');
			/* until 2026-09 roguepc used the '/save' database, shared with omega */
			RvipApp.mount(function (err) {
				if (err) status('Could not read saved games from IndexedDB (' + err + '). Saving may not work in this browser mode.', true);
				if (hasSave()) Module.arguments.push('-r');
				loadLayout();
				setTileset(L.tiles); renderTileset();
				Module.removeRunDependency('idbfs');
			}, { dir: '/save', files: ['rogue.sav', 'web-layout.json'] });
			FS.chdir(DIR);
		}],
		onRuntimeInitialized: function () { app.running = true; status(''); },
		print: function (s) { console.log(s); },
		printErr: function (s) { console.warn(s); },
		setStatus: function (s) { if (s && !app.running) status(s.replace(/\(\d+\/\d+\)/, '').trim() || 'Loading…'); },
		onAbort: function (what) { app.crashed(what); }
	};
	document.addEventListener('keydown', onKey);
	document.addEventListener('DOMContentLoaded', function () {
		mapCv = document.querySelector('#t-map canvas');
		textCv = document.querySelector('#t-text canvas');
		textCv.width = 1440; textCv.height = 800;
		textCtx = textCv.getContext('2d');
		RvipWM.dropdown($('btn-file'), $('menu-file'));
		fetch('fonts.json').then(function (r) { return r.json(); }).then(function (list) {
			[[$('sel-font'), 'face'], [mapSel, 'mapFace']].forEach(function (a) {
				list.forEach(function (n) { var o = document.createElement('option'); o.value = n; o.textContent = n.replace(/^Web(Plus|437)_/, '').replace(/_/g, ' '); a[0].appendChild(o); });
				a[0].value = (L && L[a[1]]) || '';
			});
		}).catch(function () { });
		[[$('sel-font'), 'face'], [mapSel, 'mapFace']].forEach(function (a) {
			a[0].onchange = function () { if (!L) return; L[a[1]] = this.value; saveLayout(); loadFace(this.value, true); this.blur(); };
		});
		$('btn-tileset').onclick = toggleTileset;
		renderTileset();
		$('btn-restart').onclick = function () { location.reload(); };
		document.querySelectorAll('button').forEach(function (b) {
			b.addEventListener('mousedown', function (e) { e.preventDefault(); });
		});
		mapCv.addEventListener('mousedown', function (e) {
			var p = canvasXY(mapCv, e);
			click(1 + Math.floor(p.y / cellH()), Math.floor(p.x / L.tile));
		});
		textCv.addEventListener('mousedown', function (e) {
			var p = canvasXY(textCv, e);
			click(Math.floor(p.y / p.h * 25), Math.floor(p.x / p.w * 80));
		});
		/* a click on a pop-up cell: its row, and the column from the font's width */
		$('pop').addEventListener('mousedown', function (e) {
			var pre = this.firstElementChild, d = e.target.closest && e.target.closest('#pop pre > div');
			if (!d) return;
			var m = document.createElement('span');
			m.textContent = 'MMMMMMMMMM'; d.appendChild(m);
			var cw = m.getBoundingClientRect().width / 10;
			d.removeChild(m);
			click(popRow + Array.prototype.indexOf.call(pre.children, d), popCol + Math.floor((e.clientX - d.getBoundingClientRect().left) / cw));
		});
		document.addEventListener('contextmenu', function (e) { if (app.running && (e.target.tagName === 'CANVAS' || (e.target.closest && e.target.closest('#pop')))) { e.preventDefault(); events.push(27); } });
	});
	var resizeTimer = 0;
	window.addEventListener('resize', function () {
		if (!L) return;
		clearTimeout(resizeTimer);
		resizeTimer = setTimeout(function () {
			if (L.auto) L.tile = defaultLayout().tile;
			applyDom();
		}, 150);
	});
})();
