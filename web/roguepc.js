/*
 * Rogue PC in the browser: draws the frames port/fe_web.c sends (Module.rp).
 * Tiles mode: tiling windows (map, messages, status, inventory, pop-up over
 * the map) like the other web ports. Text mode: one window with the original
 * IBM text screen (VGA 9x16 font, CP437, CGA colours, blink). Keyboard,
 * mouse, saves in IndexedDB. Loaded before roguepc-core.js.
 */
(function () {
	'use strict';

	var DIR = '/save', SAVE = DIR + '/rogue.sav', LAYOUT_FILE = DIR + '/web-layout.json';
	var FONT = '"DejaVu Sans Mono", Menlo, Consolas, "Liberation Mono", monospace';
	var GUT = 6, TITLE_H = 20, BORDER = 2;
	var TILE_STEPS = [12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 56, 64];
	var FONT_MIN = 8, FONT_MAX = 28;
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

	var events = [], clickAt = 0, running = false;
	var dpr = Math.max(1, Math.min(3, window.devicePixelRatio || 1));
	var L = null, rects = {};
	var fontSheets = [], tileSheets = [], tileCol = null;
	var TW = 16, TH = 24;              /* ClassicRogue sprites (tiles by Oryx), 1 bit */
	var kind = 'text';                 /* what C sent last: 'text' or 'tiles' */
	var F = null;                      /* last tiles frame */
	var T = null;                      /* last text frame */
	var hist = [];
	var hero = { y: 0, x: 0 }, off = { x: 0, y: 0 }, lastLevel = -1;
	var mapCv, mapCtx, mapPrev = null;

	function $(id) { return document.getElementById(id); }
	function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
	function status(msg, isError) {
		var s = $('status');
		s.textContent = msg;
		s.className = isError ? 'error' : '';
		s.hidden = !msg;
	}
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

	/* second tile set: DawnHack in full colour, 16x16, same sprite numbers (port/mkdawn.py);
	   covers every slot the map uses; never mixed with Oryx. Per-browser preference. */
	var dawn = new Image(), dawnHas = [], useDawn = false;
	try { useDawn = localStorage.getItem('tileset') === 'dawn'; } catch (err) { /* no storage */ }
	dawn.onload = function () {
		var cv = document.createElement('canvas'); cv.width = dawn.width; cv.height = dawn.height;
		var cx = cv.getContext('2d'); cx.drawImage(dawn, 0, 0);
		var d = cx.getImageData(0, 0, cv.width, cv.height).data;
		for (var t = 0; t < (cv.width / 16) * (cv.height / 16); t++)
			dawnHas[t] = d[(((t >> 5) * 16 + 8) * cv.width + (t & 31) * 16 + 8) * 4 + 3] > 0;
		if (useDawn && L) { applyDom(); }
	};
	dawn.src = 'tiles-dawn.png';
	function dawnOn() { return useDawn && dawnHas.length > 0; }
	function renderTileset() { $('btn-tileset').textContent = 'Tile set: ' + (useDawn ? 'DawnHack' : 'Oryx'); }
	function toggleTileset() {
		useDawn = !useDawn;
		try { localStorage.setItem('tileset', useDawn ? 'dawn' : 'oryx'); } catch (err) { /* no storage */ }
		renderTileset(); applyDom();
	}
	/* sprite t in CGA colour c, cell w x h */
	function tile(ctx, t, c, x, y, w, h) {
		if (dawnOn() && dawnHas[t]) ctx.drawImage(dawn, (t & 31) * 16, (t >> 5) * 16, 16, 16, x, y, w, h);
		else ctx.drawImage(tileSheets[c], (t & 31) * TW, (t >> 5) * TH, TW, TH, x, y, w, h);
	}
	function cellH() { return dawnOn() ? L.tile : L.tile * TH / TW; }

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

	/* ---------- tiles mode: text windows ---------- */
	function measure(px) {
		var c = document.createElement('canvas').getContext('2d');
		c.font = px + 'px ' + FONT;
		return Math.ceil(c.measureText('M').width);
	}
	function cp(g) { return CP437.charAt(g); }

	/* a canvas that fills its window's body */
	function textPane(id) {
		var r = rects[id], cv = document.querySelector('#t-' + id + ' canvas');
		var w = Math.max(1, r[2] - BORDER), h = Math.max(1, r[3] - BORDER - ($('game').classList.contains('wm-single') ? 0 : TITLE_H));
		cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
		cv.style.width = w + 'px'; cv.style.height = h + 'px';
		var c = cv.getContext('2d');
		c.setTransform(dpr, 0, 0, dpr, 0, 0);
		c.fillStyle = '#000'; c.fillRect(0, 0, w, h);
		var f = L.font[id];
		c.font = f + 'px ' + FONT; c.textBaseline = 'middle';
		return { c: c, w: w, h: h, cw: measure(f), ch: Math.round(f * 1.3) };
	}
	/* one line of vram cells (attr << 8 | char) */
	function cells(P, arr, start, n, y, x0) {
		for (var i = 0; i < n; i++) {
			var v = arr[start + i], a = v >> 8, g = v & 255, x = x0 + i * P.cw;
			if ((a >> 4) & 7) { P.c.fillStyle = PAL[(a >> 4) & 7]; P.c.fillRect(x, y, P.cw, P.ch); }
			if (g && g !== 32) { P.c.fillStyle = PAL[a & 15]; P.c.fillText(cp(g), x, y + P.ch / 2 + 1); }
		}
	}
	function line(P, s, y, color) {
		P.c.fillStyle = typeof color === 'string' ? color : PAL[color];
		for (var i = 0; i < s.length; i++) P.c.fillText(s.charAt(i), 2 + i * P.cw, y + P.ch / 2 + 1);
	}
	function rowText(arr, r) {
		var s = '';
		for (var i = 0; i < 80; i++) s += cp(arr[r * 80 + i] & 255);
		return s.replace(/\s+$/, '');
	}

	function drawMsg() {
		if (!rects.msg) return;
		var P = textPane('msg'), rows = Math.max(1, Math.floor(P.h / P.ch));
		var lines = hist.map(function (s) { return { s: s, c: 7 }; });
		if (lines.length) lines[lines.length - 1].c = 15;
		/* the live message line: prompts, --More-- */
		var live = F && !(F.pop && F.pop[0] === 0) ? rowText(F.vr, 0) : '';
		var last = hist.length ? hist[hist.length - 1] : '';
		if (live && live.trim() !== last.trim()) lines.push({ s: live, c: 14, live: true });
		lines = lines.slice(-rows);
		lines.forEach(function (l, i) {
			line(P, l.s, i * P.ch, l.c);
			if (l.live && F.con && F.cr === 0 && curOn()) {
				P.c.fillStyle = PAL[14];
				P.c.fillRect(2 + F.cc * P.cw, i * P.ch + P.ch - 3, P.cw, 2);
			}
		});
	}
	function drawStat() {
		if (!rects.stat) return;
		var P = textPane('stat');
		if (!F) return;
		cells(P, F.scr, 23 * 80, 80, 0, 2);
		cells(P, F.scr, 24 * 80, 80, P.ch, 2);
	}
	function drawInv() {
		if (!rects.inv) return;
		var P = textPane('inv');
		if (!F) return;
		F.inv.forEach(function (l, i) { line(P, l.s, i * P.ch, l.c); });   /* colours from the game */
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
		c.font = 'bold ' + Math.round(s * 0.9) + 'px ' + FONT;
		c.textAlign = 'center'; c.textBaseline = 'middle';
		for (i = 0; i < 22 * 80; i++) {
			var v = F.scr[80 + i], t = F.t[i], u = F.u[i];
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

	/* ---------- tiles mode: pop-up over the map ---------- */
	function drawPop() {
		var el = $('pop');
		if (!F || !F.pop) { el.hidden = true; return; }
		var p = F.pop, rows = p[2] - p[0] + 1, cols = p[3] - p[1] + 1;
		var f = L.font.pop, cw = measure(f), ch = Math.round(f * 1.3), pad = cw;
		var w = cols * cw + 2 * pad, h = rows * ch + 2 * pad, cv = el.querySelector('canvas');
		var A = areaSize(), sc = Math.min(1, (A.w - 16) / w, (A.h - 16) / h);
		cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr);
		cv.style.width = w * sc + 'px'; cv.style.height = h * sc + 'px';
		var c = cv.getContext('2d');
		c.setTransform(dpr, 0, 0, dpr, 0, 0);
		c.fillStyle = '#000'; c.fillRect(0, 0, w, h);
		c.font = f + 'px ' + FONT; c.textBaseline = 'middle';
		var P = { c: c, cw: cw, ch: ch };
		for (var r = 0; r < rows; r++) cells(P, F.vr, (p[0] + r) * 80 + p[1], cols, pad + r * ch, pad);
		if (F.con && F.cr >= p[0] && F.cr <= p[2] && curOn()) {
			c.fillStyle = PAL[15];
			c.fillRect(pad + (F.cc - p[1]) * cw, pad + (F.cr - p[0]) * ch + ch - 3, cw, 2);
		}
		/* near where the original draws it, but inside the page */
		var m = rects.map, x = m[0] + p[1] * L.tile - off.x, y = m[1] + Math.max(0, p[0] - 1) * cellH() - off.y;
		el.style.left = clamp(x, 4, Math.max(4, A.w - w * sc - 4)) + 'px';
		el.style.top = clamp(y, 4, Math.max(4, A.h - h * sc - 4)) + 'px';
		el.pop = { r0: p[0], c0: p[1], sc: sc, cw: cw, ch: ch, pad: pad };
		el.hidden = false;
	}

	function drawTiles(full) {
		if (full) shapeMap();
		drawMap(); drawMsg(); drawStat(); drawInv(); drawPop();
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
		var font = W >= 1600 ? 14 : 13, tile = TILE_STEPS[0];
		TILE_STEPS.forEach(function (t) { if (80 * t + BORDER <= W && 22 * t * TH / TW + BORDER <= H * 0.66) tile = t; });
		var mapH = 22 * tile * TH / TW + BORDER, lower = H - mapH - GUT;
		var statH = TITLE_H + BORDER + 2 * Math.round(font * 1.3) + 4;
		return { v: 1, mode: 'tiles', tile: tile, auto: true, font: { msg: font, stat: font, inv: font, pop: font + 2 },
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
				Object.keys(d.font).forEach(function (k) {
					if (s.font && s.font[k] >= FONT_MIN && s.font[k] <= FONT_MAX) d.font[k] = s.font[k];
				});
				if (s.mode === 'text') d.mode = 'text';
				if (s.wm) d.wm = s.wm;
			}
		} catch (err) { /* nothing saved yet */ }
		L = d;
	}
	var saveTimer = 0;
	function saveLayout() {
		clearTimeout(saveTimer);
		saveTimer = setTimeout(function () {
			try { Module.FS.writeFile(LAYOUT_FILE, JSON.stringify(L)); syncFiles(); }
			catch (err) { console.warn('layout not saved', err); }
		}, 400);
	}
	function place(el, r) {
		el.style.left = r[0] + 'px'; el.style.top = r[1] + 'px';
		el.style.width = Math.max(0, r[2]) + 'px'; el.style.height = Math.max(0, r[3]) + 'px';
	}
	function showText() { return L.mode === 'text' || kind === 'text'; }

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
		var line = Math.round(L.font.msg * 1.3) + 4, stat = 2 * Math.round(L.font.stat * 1.3) + 4;
		wm = RvipWM({
			area: $('game'), menu: $('btn-layout'),
			wins: [{ id: 'map', title: 'Map' }, { id: 'msg', title: 'Messages' }, { id: 'stat', title: 'Status' }, { id: 'inv', title: 'Inventory' }, { id: 'vis', title: 'Visible' }],
			multi: { d: 'v', r: d.bottom, a: 'map', b: { d: 'h', r: 0.4, a: { d: 'v', r: d.stat, a: 'msg', b: 'stat' }, b: { d: 'h', r: 0.5, a: 'inv', b: 'vis' } } },
			single: { d: 'v', r: line / A.h, a: 'msg', b: { d: 'v', r: 1 - stat / (A.h - line), a: 'map', b: 'stat' } },
			state: L.wm, noFont: 'map',
			save: function (st) { L.wm = st; saveLayout(); },
			layout: function (r) {
				var A = areaSize(), txt = showText();
				rects = r; rects.text = [0, 0, A.w, A.h];
				$('t-text').hidden = !txt;
				place($('t-text'), rects.text);
				$('btn-tiles').classList.toggle('on', L.mode === 'tiles');
				$('btn-text').classList.toggle('on', L.mode === 'text');
				['btn-zoom-in', 'btn-zoom-out'].forEach(function (b) { $(b).disabled = L.mode === 'text'; });
				$('vis').style.fontSize = (L.font.vis || 13) + 'px';
				if (txt) { $('pop').hidden = true; fitText(); drawText(); }
				else { drawTiles(true); scrollMap(true); drawPop(); }
			},
			font: function (id, d) {
				if (id === 'vis') { L.font.vis = clamp((L.font.vis || 13) + d, FONT_MIN, FONT_MAX); applyDom(); saveLayout(); }
				else zoomText(id, d);
			},
			onReset: resetLayout
		});
	}
	function setMode(m) {
		if (!L || L.mode === m) return;
		L.mode = m;
		if (m === 'text' && F) T = { scr: F.vr, cr: F.cr, cc: F.cc, con: F.con, pic: null };
		applyDom(); saveLayout();
	}

	function zoomMap(d) {
		var i = clamp(TILE_STEPS.indexOf(L.tile) + d, 0, TILE_STEPS.length - 1);
		L.tile = TILE_STEPS[i]; L.auto = false;
		applyDom(); saveLayout();
		status('Map tiles: ' + L.tile + ' px');
		setTimeout(function () { status(''); }, 1200);
	}
	function zoomText(id, d) {
		L.font[id] = clamp(L.font[id] + d, FONT_MIN, FONT_MAX);
		L.font.pop = L.font[id] + 2;             /* pop-ups follow the last zoomed window */
		applyDom(); saveLayout();
	}
	function resetLayout() {
		var m = L.mode;
		L = defaultLayout(); L.mode = m; L.wm = wm.state();
		applyDom(); saveLayout();
	}

	/* ---------- called by the game (port/fe_web.c) ---------- */
	var autoMore = 0;
	function renderMore() { $('btn-more').textContent = 'auto_more: ' + (autoMore ? 'on' : 'off'); $('btn-more').classList.toggle('on', !!autoMore); }
	var rp = {
		init: function (font, tiles, cols, ntiles, am) {
			buildSheets(font, tiles, cols, ntiles);
			autoMore = am; renderMore();
			if (!L) loadLayout();
			$('game').hidden = false;
			applyDom();
		},
		text: function (scr, cr, cc, con, pic) {
			T = { scr: Module.HEAPU16.slice(scr >> 1, (scr >> 1) + 2000), cr: cr, cc: cc, con: con,
				pic: pic ? Module.HEAPU8.slice(pic, pic + 64000) : null };
			if (kind !== 'text') { kind = 'text'; applyDom(); }
			else drawText();
		},
		tiles: function (scr, vr, t, u, inv, at, ninv, pr0, pc0, pr1, pc1, cr, cc, con, hy, hx, lvl) {
			var H = Module.HEAPU8, lines = [];
			for (var i = 0; i < ninv; i++) {
				var s = '', o = inv + i * 81;
				while (H[o] && s.length < 80) s += cp(H[o++]);
				lines.push({ s: s, c: H[at + i] & 15 });
			}
			F = { scr: Module.HEAPU16.slice(scr >> 1, (scr >> 1) + 2000), vr: Module.HEAPU16.slice(vr >> 1, (vr >> 1) + 2000),
				t: Module.HEAP32.slice(t >> 2, (t >> 2) + 1760), u: Module.HEAP32.slice(u >> 2, (u >> 2) + 1760),
				inv: lines, pop: pr0 >= 0 ? [pr0, pc0, pr1, pc1] : null, cr: cr, cc: cc, con: con };
			T = { scr: F.vr, cr: cr, cc: cc, con: con, pic: null };
			var moved = hy !== hero.y || hx !== hero.x;
			hero.y = hy; hero.x = hx;
			if (kind !== 'tiles') { kind = 'tiles'; lastLevel = lvl; applyDom(); return; }
			if (showText()) { drawText(); return; }
			drawMap(); drawMsg(); drawStat(); drawInv(); drawPop();
			if (moved || lvl !== lastLevel) scrollMap(lvl !== lastLevel);
			lastLevel = lvl;
		},
		vis: function (s) {
			RvipWM.visible($('vis'), s.replace(/^([MI])([0-9a-f]{2})/gm, function (m, k, h) { return k + cp(parseInt(h, 16)); })
				.replace(/\t(\d+)$/gm, function (m, c) { return '\t' + PAL[+c]; }));
		},
		/* fold: the game folded a repeat into "message (xN)", replacing the last line */
		msg: function (s, fold) {
			if (fold && hist.length) { hist[hist.length - 1] = s; return; }
			hist.push(s);
			if (hist.length > 400) hist.shift();
		},
		toggle: function () { setMode(L.mode === 'text' ? 'tiles' : 'text'); },
		key: function () { return events.length ? events.shift() : -1; },
		click: function () { return clickAt; },
		pending: function () { return events.length ? 1 : 0; },
		flush: function () { events.length = 0; },
		sync: function () { syncFiles(); },
		end: function (saved) {
			running = false;
			syncFiles(function () {
				$('overlay-msg').textContent = saved ? 'Your game has been saved. Play again to continue it.' : 'The game is over.';
				$('overlay').hidden = false;
			});
		}
	};

	/* blinking attribute and cursor */
	setInterval(function () {
		if (!running || !L) return;
		if (showText()) drawText();
		else if (F && F.con) { if (F.cr === 0) drawMsg(); else if (F.pop) drawPop(); }
	}, 115);

	/* ---------- input ---------- */
	function onKey(e) {
		if (!$('help').hidden) {
			if (e.key === 'Escape') { $('help').hidden = true; e.preventDefault(); }
			return;
		}
		if (!running || e.isComposing || e.metaKey) return;
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
		if (!running || r < 0 || r > 24 || c < 0 || c > 79) return;
		clickAt = (r << 8) | c;
		events.push(FK_CLICK);
	}
	function canvasXY(cv, e) {
		var b = cv.getBoundingClientRect();
		return { x: e.clientX - b.left, y: e.clientY - b.top, w: b.width, h: b.height };
	}

	/* ---------- saves: IndexedDB (IDBFS) ---------- */
	var syncing = false, syncAgain = false, pendingCbs = [];
	function syncFiles(cb) {
		if (!Module.FS) { if (cb) cb(); return; }
		if (typeof cb === 'function') pendingCbs.push(cb);
		if (syncing) { syncAgain = true; return; }
		syncing = true;
		var cbs = pendingCbs; pendingCbs = [];
		Module.FS.syncfs(false, function (err) {
			syncing = false;
			if (err) status('Saving to browser storage (IndexedDB) failed: ' + err + '. Use "Export save" to keep a copy.', true);
			cbs.forEach(function (f) { f(err); });
			if (syncAgain) { syncAgain = false; syncFiles(); }
		});
	}
	function hasSave() { try { Module.FS.stat(SAVE); return true; } catch (e) { return false; } }
	function exportSave() {
		if (!hasSave()) { status('There is no saved game yet.', true); setTimeout(function () { status(''); }, 2000); return; }
		var a = document.createElement('a');
		a.href = URL.createObjectURL(new Blob([Module.FS.readFile(SAVE)], { type: 'application/octet-stream' }));
		a.download = 'rogue.sav';
		document.body.appendChild(a); a.click();
		setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
	}
	function importSave(file) {
		var r = new FileReader();
		r.onload = function () {
			if (!confirm('Replace the current game with "' + file.name + '"?')) return;
			running = false;
			Module.FS.writeFile(SAVE, new Uint8Array(r.result));
			syncFiles(function (err) { if (!err) location.reload(); });
		};
		r.readAsArrayBuffer(file);
	}
	function newGame() {
		if (!confirm('Delete the saved game in this browser and start a new one?')) return;
		running = false;
		if (hasSave()) Module.FS.unlink(SAVE);
		syncFiles(function (err) { if (!err) location.reload(); });
	}

	/* ---------- help ---------- */
	var helpLoaded = false;
	function toggleHelp() {
		var h = $('help');
		h.hidden = !h.hidden;
		if (!h.hidden && !helpLoaded) {
			helpLoaded = true;
			fetch('help.html').then(function (r) { if (!r.ok) throw new Error(r.status); return r.text(); })
				.then(function (t) { $('help-body').innerHTML = t; })
				.catch(function (err) { helpLoaded = false; $('help-body').textContent = 'Could not load the guide (' + err + '). Press F1 in the game for its own help.'; });
		}
		if (!h.hidden) $('help-body').focus();
	}

	/* ---------- startup ---------- */
	window.Module = {
		rp: rp,
		arguments: [],
		preRun: [function () {
			var FS = Module.FS;
			FS.mkdirTree(DIR);
			FS.mount(Module.IDBFS, {}, DIR);
			FS.chdir(DIR);
			Module.addRunDependency('idbfs');
			FS.syncfs(true, function (err) {
				if (err) status('Could not read saved games from IndexedDB (' + err + '). Saving may not work in this browser mode.', true);
				if (hasSave()) Module.arguments.push('-r');
				loadLayout();
				Module.removeRunDependency('idbfs');
			});
		}],
		onRuntimeInitialized: function () { running = true; status(''); },
		print: function (s) { console.log(s); },
		printErr: function (s) { console.warn(s); },
		setStatus: function (s) { if (s && !running) status(s.replace(/\(\d+\/\d+\)/, '').trim() || 'Loading…'); },
		onAbort: function (what) { crashed(what); }
	};
	function crashed(err) {
		if (!running) return;
		running = false;
		var msg = (err && (err.message || err.reason && err.reason.message)) || String(err);
		console.error('[roguepc] crash:', err);
		status('The game crashed (' + msg + '). Reload the page to continue from the last autosave.', true);
	}
	window.addEventListener('unhandledrejection', function (e) {
		if (e.reason && e.reason.name === 'ExitStatus') return;   /* exit() is the normal end */
		crashed(e.reason);
	});
	window.addEventListener('error', function (e) {
		if (e.error && e.error.name === 'ExitStatus') return;
		if (e.error instanceof WebAssembly.RuntimeError || /roguepc-core/.test(e.filename || '')) crashed(e.error || e.message);
	});

	document.addEventListener('keydown', onKey);
	document.addEventListener('DOMContentLoaded', function () {
		mapCv = document.querySelector('#t-map canvas');
		textCv = document.querySelector('#t-text canvas');
		textCv.width = 1440; textCv.height = 800;
		textCtx = textCv.getContext('2d');
		$('btn-export').onclick = exportSave;
		$('btn-import').onclick = function () { $('import-file').click(); };
		$('import-file').onchange = function () { if (this.files[0]) importSave(this.files[0]); this.value = ''; };
		$('btn-new').onclick = newGame;
		$('btn-help').onclick = toggleHelp;
		$('help-close').onclick = toggleHelp;
		$('btn-zoom-in').onclick = function () { zoomMap(1); };
		$('btn-zoom-out').onclick = function () { zoomMap(-1); };
		$('btn-tiles').onclick = function () { setMode('tiles'); };
		$('btn-text').onclick = function () { setMode('text'); };
		$('btn-tileset').onclick = toggleTileset;
		renderTileset();
		$('btn-more').onclick = function () { autoMore = autoMore ? 0 : 1; renderMore(); Module._web_set_auto_more(autoMore); };
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
		$('pop').addEventListener('mousedown', function (e) {
			var P = this.pop, p = canvasXY(this.querySelector('canvas'), e);
			if (P) click(P.r0 + Math.floor((p.y / P.sc - P.pad) / P.ch), P.c0 + Math.floor((p.x / P.sc - P.pad) / P.cw));
		});
		document.addEventListener('contextmenu', function (e) { if (running && e.target.tagName === 'CANVAS') { e.preventDefault(); events.push(27); } });
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
