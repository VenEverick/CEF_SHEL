/* SHELTER — мост между вёрсткой (UI) и нативной оболочкой на CEF.
 *
 * Подключается автоматически при отдаче index.html (см. ui_scheme.cc) и создаёт
 * window.shelterNative + хуки window.shelter*, которые ожидает вёрстка.
 * Вкладки — отдельные нативные вид'ы (CefBrowserView) поверх области #viewport.
 * Пока поверх страницы открыто HTML-меню/модальное окно/тост, нативный вид
 * заменяется снимком страницы (иначе HTML оказался бы под нативным видом).
 */
(function () {
  'use strict';
  if (window.shelterNative) return;

  var PLATFORM = '__PLATFORM__';
  /* «Дыры» и скругление нативного вида вкладки: только macOS (на Windows содержимое рисует
     общая DirectComposition-поверхность, окнам HWND область не помогает). */
  var USE_HOLES = PLATFORM === 'darwin';
  var VERSION = '__APP_VERSION__';
  var root = document.documentElement;

  /* ---------- cefQuery ---------- */
  function q(method, args) {
    return new Promise(function (resolve) {
      if (typeof window.cefQuery !== 'function') { resolve(null); return; }
      window.cefQuery({
        request: JSON.stringify({ m: method, a: args || {} }),
        persistent: false,
        onSuccess: function (r) {
          var v = {};
          try { v = r ? JSON.parse(r) : {}; } catch (_) {}
          resolve(v);
        },
        onFailure: function () { resolve(null); }
      });
    });
  }
  function qSnap(id, quality, ms) {
    return new Promise(function (resolve) {
      if (typeof window.cefQuery !== 'function') { resolve(''); return; }
      var done = false;
      var timer = setTimeout(function () { if (!done) { done = true; resolve(''); } }, ms || 900);
      window.cefQuery({
        request: JSON.stringify({ m: 'view.snap', a: { id: id, q: quality || 72 } }),
        persistent: false,
        onSuccess: function (r) { if (!done) { done = true; clearTimeout(timer); resolve(r || ''); } },
        onFailure: function () { if (!done) { done = true; clearTimeout(timer); resolve(''); } }
      });
    });
  }

  /* ---------- шифрование секретов (синхронное; ключ выдаёт нативная часть) ---------- */
  var SECRET_KEY = '__SECRET_KEY__';
  var Sec = (function () {
    var K256 = new Uint32Array([
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);
    function rotr(x, n) { return (x >>> n) | (x << (32 - n)); }
    function sha256(msg) {
      var h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
      var l = msg.length, total = ((l + 9 + 63) >> 6) << 6;
      var m = new Uint8Array(total);
      m.set(msg); m[l] = 0x80;
      var dv = new DataView(m.buffer);
      dv.setUint32(total - 8, Math.floor((l * 8) / 4294967296)); dv.setUint32(total - 4, (l * 8) >>> 0);
      var w = new Uint32Array(64);
      for (var o = 0; o < total; o += 64) {
        var t;
        for (t = 0; t < 16; t++) w[t] = dv.getUint32(o + t * 4);
        for (t = 16; t < 64; t++) {
          var s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >>> 3);
          var s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >>> 10);
          w[t] = (w[t - 16] + s0 + w[t - 7] + s1) | 0;
        }
        var a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (t = 0; t < 64; t++) {
          var S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
          var ch = (e & f) ^ (~e & g);
          var t1 = (hh + S1 + ch + K256[t] + w[t]) | 0;
          var S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
          var mj = (a & b) ^ (a & c) ^ (b & c);
          var t2 = (S0 + mj) | 0;
          hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
        }
        h[0] = (h[0] + a) | 0; h[1] = (h[1] + b) | 0; h[2] = (h[2] + c) | 0; h[3] = (h[3] + d) | 0;
        h[4] = (h[4] + e) | 0; h[5] = (h[5] + f) | 0; h[6] = (h[6] + g) | 0; h[7] = (h[7] + hh) | 0;
      }
      var out = new Uint8Array(32), odv = new DataView(out.buffer);
      for (var i = 0; i < 8; i++) odv.setUint32(i * 4, h[i]);
      return out;
    }
    function cat() {
      var n = 0, i;
      for (i = 0; i < arguments.length; i++) n += arguments[i].length;
      var r = new Uint8Array(n), p = 0;
      for (i = 0; i < arguments.length; i++) { r.set(arguments[i], p); p += arguments[i].length; }
      return r;
    }
    function hmac(key, data) {
      if (key.length > 64) key = sha256(key);
      var ip = new Uint8Array(64), op = new Uint8Array(64);
      for (var i = 0; i < 64; i++) { var k = i < key.length ? key[i] : 0; ip[i] = k ^ 0x36; op[i] = k ^ 0x5c; }
      return sha256(cat(op, sha256(cat(ip, data))));
    }
    function rotl(x, n) { return (x << n) | (x >>> (32 - n)); }
    function chacha20(key, nonce, data) {          // RFC 8439, счётчик с 1
      var st = new Uint32Array(16), x = new Uint32Array(16), out = new Uint8Array(data.length);
      var kv = new DataView(key.buffer, key.byteOffset, 32), nv = new DataView(nonce.buffer, nonce.byteOffset, 12);
      st[0] = 0x61707865; st[1] = 0x3320646e; st[2] = 0x79622d32; st[3] = 0x6b206574;
      for (var i = 0; i < 8; i++) st[4 + i] = kv.getUint32(i * 4, true);
      for (i = 0; i < 3; i++) st[13 + i] = nv.getUint32(i * 4, true);
      function qr(a, b, c, d) {
        x[a] = (x[a] + x[b]) | 0; x[d] = rotl(x[d] ^ x[a], 16);
        x[c] = (x[c] + x[d]) | 0; x[b] = rotl(x[b] ^ x[c], 12);
        x[a] = (x[a] + x[b]) | 0; x[d] = rotl(x[d] ^ x[a], 8);
        x[c] = (x[c] + x[d]) | 0; x[b] = rotl(x[b] ^ x[c], 7);
      }
      for (var pos = 0, ctr = 1; pos < data.length; pos += 64, ctr++) {
        st[12] = ctr;
        x.set(st);
        for (var r = 0; r < 10; r++) {
          qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15);
          qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14);
        }
        for (i = 0; i < 16; i++) {
          var v = (x[i] + st[i]) | 0;
          for (var j = 0; j < 4; j++) {
            var p = pos + i * 4 + j;
            if (p < data.length) out[p] = data[p] ^ ((v >>> (8 * j)) & 255);
          }
        }
      }
      return out;
    }
    function enc8(s) { return new TextEncoder().encode(s); }
    function fromHex(h) {
      var n = h.length >> 1, o = new Uint8Array(n);
      for (var i = 0; i < n; i++) o[i] = parseInt(h.substr(i * 2, 2), 16);
      return o;
    }
    function b64(u) { var s = ''; for (var i = 0; i < u.length; i++) s += String.fromCharCode(u[i]); return btoa(s); }
    function unb64(s) { var b = atob(s), u = new Uint8Array(b.length); for (var i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u; }
    var keys = null;
    function getKeys() {
      if (keys) return keys;
      if (!/^[0-9a-f]{64}$/.test(SECRET_KEY)) return null;
      var m = fromHex(SECRET_KEY);
      keys = { ek: hmac(m, enc8('shelter/enc')), mk: hmac(m, enc8('shelter/mac')) };
      return keys;
    }
    function randomBytes(n) { var r = new Uint8Array(n); crypto.getRandomValues(r); return r; }
    function encrypt(text, nonceForTest) {
      var k = getKeys();
      if (!k) return '';
      var nonce = nonceForTest || randomBytes(12);
      var ct = chacha20(k.ek, nonce, enc8(String(text)));
      var tag = hmac(k.mk, cat(nonce, ct));
      return 'ss1:' + b64(cat(nonce, ct, tag));
    }
    function decrypt(s) {
      var k = getKeys();
      if (!k || typeof s !== 'string' || s.indexOf('ss1:') !== 0) return '';
      var raw;
      try { raw = unb64(s.slice(4)); } catch (_) { return ''; }
      if (raw.length < 12 + 32) return '';
      var nonce = raw.subarray(0, 12), ct = raw.subarray(12, raw.length - 32), tag = raw.subarray(raw.length - 32);
      var want = hmac(k.mk, cat(nonce, ct)), diff = 0;
      for (var i = 0; i < 32; i++) diff |= want[i] ^ tag[i];
      if (diff) return '';
      try { return new TextDecoder().decode(chacha20(k.ek, nonce, ct)); } catch (_) { return ''; }
    }
    return { enc: encrypt, dec: decrypt, sha256: sha256, hmac: hmac, chacha20: chacha20, _setKey: function (h) { SECRET_KEY = h; keys = null; } };
  })();

  /* ---------- состояние ---------- */
  var st = {
    visible: false,     // нативный вид вкладки должен быть показан
    tabId: '',
    urls: {},           // id -> последний известный url
    loading: {},        // id -> bool
    zoom: 1,
    frozen: false,      // вместо нативного вида показан снимок
    busy: false,
    snapFor: '',
    snapUrl: '',
    lastRect: ''
  };
  var cbs = { dlprompt: null, ctxclose: null };

  function byId(id) { return document.getElementById(id); }
  function vpRect() {
    var v = byId('viewport');
    if (!v) return null;
    var r = v.getBoundingClientRect();
    var o = { x: r.left, y: r.top, w: r.width, h: r.height };
    // Панель поиска (HTML) лежит поверх области вида — освобождаем ей место сверху.
    var fb = byId('findbar');
    if (st.visible && fb && !fb.hidden) {
      var fr = fb.getBoundingClientRect();
      var cut = fr.bottom + 6 - o.y;
      if (fr.height > 0 && cut > 0 && cut < o.h - 40) { o.y += cut; o.h -= cut; }
    }
    return o;
  }
  function rectKey(r) { return r ? [r.x, r.y, r.w, r.h].map(Math.round).join(',') : ''; }
  function editableFocused() {
    var a = document.activeElement;
    return !!a && (/^(INPUT|TEXTAREA|SELECT)$/.test(a.tagName) || a.isContentEditable);
  }
  function activeId() {
    try { return typeof window.getActiveTabId === 'function' ? (window.getActiveTabId() || '') : ''; }
    catch (_) { return ''; }
  }
  function tabInfo(id) {
    try { if (typeof window.__shelterTab === 'function') return window.__shelterTab(id); } catch (_) {}
    var space = 'main', ghost = false;
    try {
      space = localStorage.getItem('shelter:currentSpace') || 'main';
      ghost = localStorage.getItem('shelter:ghost') === '1';
    } catch (_) {}
    return { space: space, ghost: ghost };
  }

  /* ---------- стили ---------- */
  var style = document.createElement('style');
  style.textContent =
    (USE_HOLES ? '.sh-snap-wait .menu,.sh-snap-wait .scrim{visibility:hidden!important}'
               : '.sh-snap-wait .menu,.sh-snap-wait .scrim,.sh-snap-wait #toasts,.sh-snap-wait .suggest{visibility:hidden!important}') +
    /* Windows: пиксели страницы нельзя обрезать по маске окна, поэтому скругление сцены делаем мелким */
    (USE_HOLES ? '' : '.stage{border-radius:6px!important}') +
    '#shSnap{position:absolute;left:0;top:0;width:100%;height:100%;object-fit:fill;pointer-events:none;z-index:3;background:#fff}' +
    (PLATFORM === 'darwin' ? '#winLights{visibility:hidden!important}' : '');
  (document.head || root).appendChild(style);
  root.classList.add('host-' + PLATFORM);

  /* ---------- снимок вместо нативного вида ---------- */
  /* Модальные/интерактивные слои, которым нужен снимок вместо нативного вида: меню, модалки. */
  var FREEZE_BASE = '.menu:not(.closing),.scrim:not(.closing),.call:not([hidden]),#findbar:not([hidden])';
  /* Лёгкие слои (подсказки, уведомления, подсказки омнибокса, панель загрузок) показываются через
     «дыры» в нативном виде вкладки: страница остаётся живой, без снимков и подвисаний. */
  var HOLE_SEL = '.tip,#toasts>.toast,.suggest:not([hidden]),#dlFloat:not([hidden])';
  /* без «дыр» те же слои показываем через снимок (как раньше) */
  var OVERLAY_SEL = USE_HOLES ? FREEZE_BASE : FREEZE_BASE + ',' + HOLE_SEL;

  /* ---------- замеры ---------- */
  var perf = window.__shPerf = { freezes: 0, thaws: 0, snapMs: [], snapKB: [], freezeMs: [], thawMs: [], holes: 0, clips: 0, longTasks: 0, longMax: 0 };
  function pushPerf(arr, v) { arr.push(Math.round(v)); if (arr.length > 20) arr.shift(); }
  try {
    new PerformanceObserver(function (l) {
      l.getEntries().forEach(function (e) { perf.longTasks++; if (e.duration > perf.longMax) perf.longMax = Math.round(e.duration); });
    }).observe({ entryTypes: ['longtask'] });
  } catch (_) {}

  function overlayOverViewport() {
    var vp = vpRect();
    if (!vp) return false;
    var list = document.querySelectorAll(OVERLAY_SEL);
    for (var i = 0; i < list.length; i++) {
      var r = list[i].getBoundingClientRect();
      if (r.width > 0 && r.height > 0 &&
          r.right > vp.x && r.left < vp.x + vp.w && r.bottom > vp.y && r.top < vp.y + vp.h) return true;
    }
    return false;
  }
  function showSnap(url) {
    var v = byId('viewport');
    if (!v) return;
    var img = byId('shSnap');
    if (!url) { if (img) img.remove(); return; }
    if (!img) { img = document.createElement('img'); img.id = 'shSnap'; img.alt = ''; v.appendChild(img); }
    img.src = url;
  }
  function dropSnap() { st.snapUrl = ''; st.snapFor = ''; showSnap(''); }
  function nextFrames(n) {
    return new Promise(function (res) {
      (function step(i) { if (i <= 0) res(); else requestAnimationFrame(function () { step(i - 1); }); })(n);
    });
  }

  function freeze() {
    st.busy = true;
    var id = st.tabId, t0 = performance.now(), t1 = 0;
    root.classList.add('sh-snap-wait');
    qSnap(id, 58, 900).then(function (url) {
      t1 = performance.now();
      pushPerf(perf.snapMs, t1 - t0); pushPerf(perf.snapKB, (url || '').length / 1024);
      if (url && st.tabId === id) { st.snapUrl = url; st.snapFor = id; showSnap(url); }
      return nextFrames(2);
    }).then(function () {
      st.frozen = true;
      return q('view.hide');
    }).then(function () {
      perf.freezes++; pushPerf(perf.freezeMs, performance.now() - t0);
      st.busy = false;
      root.classList.remove('sh-snap-wait');
      evaluate();
    });
  }
  function thaw() {
    st.busy = true;
    var r = vpRect(), tt0 = performance.now();
    q('view.show', { rect: r, focus: !editableFocused() }).then(function () { return nextFrames(2); }).then(function () {
      st.frozen = false;
      dropSnap();
      perf.thaws++; pushPerf(perf.thawMs, performance.now() - tt0);
      st.busy = false;
      st.clipKey = '';
      evaluate();
    });
  }

  /* ---------- скругление углов и «дыры» ---------- */
  function px(v) { v = parseFloat(v); return isNaN(v) ? 0 : v; }
  function computeRadii(vp) {
    var v = byId('viewport'), sg = byId('stage');
    if (!v) return [0, 0, 0, 0];
    var cv = getComputedStyle(v);
    var own = [px(cv.borderTopLeftRadius), px(cv.borderTopRightRadius), px(cv.borderBottomRightRadius), px(cv.borderBottomLeftRadius)];
    var res = own.slice();
    if (sg) {
      var cs = getComputedStyle(sg), sr = sg.getBoundingClientRect();
      var l = sr.left + px(cs.borderLeftWidth), t = sr.top + px(cs.borderTopWidth);
      var r = sr.right - px(cs.borderRightWidth), b = sr.bottom - px(cs.borderBottomWidth);
      var sRad = [px(cs.borderTopLeftRadius), px(cs.borderTopRightRadius), px(cs.borderBottomRightRadius), px(cs.borderBottomLeftRadius)];
      var near = function (a, c) { return Math.abs(a - c) < 2.5; };
      var hits = [near(vp.x, l) && near(vp.y, t), near(vp.x + vp.w, r) && near(vp.y, t),
                  near(vp.x + vp.w, r) && near(vp.y + vp.h, b), near(vp.x, l) && near(vp.y + vp.h, b)];
      for (var i = 0; i < 4; i++) if (hits[i] && own[i] < 1) res[i] = Math.max(0, sRad[i] - 1);
    }
    return res.map(function (x) { return Math.round(x * 10) / 10; });
  }
  function holeRects(vp) {
    var out = [], list = document.querySelectorAll(HOLE_SEL), i, j;
    for (i = 0; i < list.length; i++) {
      var r = list[i].getBoundingClientRect();
      if (r.width < 1 || r.height < 1) continue;
      var x0 = Math.max(Math.floor(r.left) - 2, vp.x), y0 = Math.max(Math.floor(r.top) - 2, vp.y);
      var x1 = Math.min(Math.ceil(r.right) + 2, vp.x + vp.w), y1 = Math.min(Math.ceil(r.bottom) + 2, vp.y + vp.h);
      if (x1 <= x0 || y1 <= y0) continue;
      out.push([x0, y0, x1, y1]);
    }
    /* объединяем пересекающиеся прямоугольники (иначе even-odd вырежет «пересечение» обратно) */
    var merged = true;
    while (merged) {
      merged = false;
      for (i = 0; i < out.length && !merged; i++) for (j = i + 1; j < out.length && !merged; j++) {
        var a = out[i], b = out[j];
        if (a[0] < b[2] && b[0] < a[2] && a[1] < b[3] && b[1] < a[3]) {
          out[i] = [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[2], b[2]), Math.max(a[3], b[3])];
          out.splice(j, 1); merged = true;
        }
      }
    }
    return out.map(function (a) { return [Math.round(a[0] - vp.x), Math.round(a[1] - vp.y), Math.round(a[2] - a[0]), Math.round(a[3] - a[1])]; });
  }
  function syncClip() {
    if (!USE_HOLES || !st.visible || st.frozen || st.busy) return 0;
    var vp = vpRect();
    if (!vp || vp.w < 2) return 0;
    var rad = computeRadii(vp), holes = holeRects(vp);
    var key = rad.join(',') + '|' + holes.join(';');
    if (key !== st.clipKey) {
      st.clipKey = key; perf.clips++; perf.holes = holes.length;
      q('view.clip', { radii: rad, holes: holes });
    }
    return holes.length;
  }
  var clipLoop = false, clipCalm = 0;
  function clipTick() {
    var n = syncClip();
    if (n > 0) clipCalm = 3; else clipCalm--;
    if (clipCalm > 0) requestAnimationFrame(clipTick); else clipLoop = false;
  }
  function kickClip() {
    clipCalm = Math.max(clipCalm, 3);
    if (!clipLoop) { clipLoop = true; requestAnimationFrame(clipTick); }
  }
  window.__shClip = function () { return st.clipKey || ''; };

  function evaluate() {
    if (st.busy) return;
    var need = st.visible && overlayOverViewport();
    if (need && !st.frozen) freeze();
    else if (!need && st.frozen && st.visible) thaw();
    else if (!st.visible && st.frozen) { st.frozen = false; dropSnap(); }
    kickClip();
  }
  var evalQueued = false;
  function scheduleEval() {
    if (evalQueued) return;
    evalQueued = true;
    requestAnimationFrame(function () { evalQueued = false; evaluate(); });
  }

  /* ---------- раскладка ---------- */
  function relayout() {
    if (!st.visible || st.frozen || st.busy) return;
    kickClip();
    var r = vpRect();
    var k = rectKey(r);
    if (!r || k === st.lastRect || r.w < 2) return;
    st.lastRect = k;
    st.clipKey = '';
    q('view.layout', { rect: r, visible: true });
  }

  /* ---------- хуки, которых ждёт вёрстка ---------- */
  window.shelterOpen = function (url, opts) {
    var id = activeId();
    var rect = vpRect();
    if (!id || !rect || rect.w < 2 || rect.h < 2) return false;
    var info = tabInfo(id);
    var partition = (info.ghost ? 'temp:ghost-' : 'persist:space-') + info.space;
    var changed = st.urls[id] !== url;
    st.urls[id] = url;
    if (st.tabId !== id) { dropSnap(); }
    st.tabId = id;
    st.visible = true;
    var focus = true;
    if (editableFocused()) {
      if (changed) { try { document.activeElement.blur(); } catch (_) {} }
      else focus = false;
    }
    st.lastRect = rectKey(rect);
    q('view.open', {
      id: id, url: url, partition: partition, rect: rect,
      visible: !st.frozen, focus: focus && !st.frozen, zoom: st.zoom
    });
    st.clipKey = '';
    scheduleEval();
    return true;
  };
  window.shelterShowChrome = function () {
    if (!st.visible && !st.frozen) return;
    st.visible = false;
    st.frozen = false;
    dropSnap();
    q('view.hide');
  };
  window.shelterIsLoading = function () { return !!st.loading[st.tabId] && st.visible; };
  window.shelterStopLoad = function () {
    if (!st.visible || !st.loading[st.tabId]) return false;
    st.loading[st.tabId] = false;
    q('view.act', { id: st.tabId, act: 'stop' });
    if (typeof window.onNativeLoading === 'function') window.onNativeLoading(false);
    return true;
  };
  window.shelterSetZoomAll = function (f) {
    f = +f;
    if (!(f > 0)) return;
    st.zoom = f;
    q('view.zoom', { factor: f });
  };
  window.shelterGhostSync = function () { scheduleSync(); };
  window.shelterAfterFire = function () { scheduleSync(); };
  window.shelterGetThumb = function () { return ''; };
  window.shelterCaptureVisibleTabs = function () {
    var id = st.tabId;
    if (!id || !st.visible) return;
    var put = function (url) {
      if (url && typeof window.shelterOnThumb === 'function') window.shelterOnThumb(id, url);
    };
    if (st.frozen || st.busy) { if (st.snapFor === id) put(st.snapUrl); return; }
    qSnap(id, 40, 900).then(put);
  };

  /* ---------- синхронизация набора вкладок ---------- */
  var syncTimer = 0;
  function syncTabs() {
    var ids = [];
    try { if (typeof window.getAllTabIds === 'function') ids = window.getAllTabIds() || []; } catch (_) {}
    if (ids.length) q('view.sync', { ids: ids });
  }
  function scheduleSync() { clearTimeout(syncTimer); syncTimer = setTimeout(syncTabs, 350); }

  /* ---------- window.shelterNative ---------- */
  window.shelterNative = {
    isNative: true,
    platform: PLATFORM,
    version: VERSION,
    minimize: function () { return q('win.minimize'); },
    maximize: function () { return q('win.maximize'); },
    close: function () { return q('win.close'); },
    clearPrivacy: function (parts, opts) {
      return q('privacy.clear', { parts: parts || null, opts: opts || null });
    },
    clipRead: function () { return q('clip.read').then(function (r) { return (r && r.text) || ''; }); },
    clipWrite: function (t) { return q('clip.write', { text: String(t == null ? '' : t) }).then(function (r) { return !!(r && r.ok); }); },
    themeScheme: function (m) { q('theme.scheme', { mode: m }); },
    onZoom: function () {},
    onCtxClose: function (cb) { cbs.ctxclose = cb; },
    ctxAct: function (o) { return q('ctx.act', o || {}); },
    openPlainWindow: function (o) { return q('win.openPlain', { url: (o && o.url) || '' }); },
    showInFolder: function (p) { return q('shell.show', { path: String(p || '') }); },
    onDownloadPrompt: function (cb) { cbs.dlprompt = cb; },
    downloadDecision: function (o) { return q('dl.decision', o || {}); },
    secretEnc: function (t) { return Sec.enc(t); },
    secretDec: function (t) { return Sec.dec(t); }
    // authWindow / devTools — не определены: вёрстка использует запасной путь.
  };

  /* ---------- события host -> UI ---------- */
  function known(id) {
    try { return (window.getAllTabIds ? window.getAllTabIds() : []).indexOf(id) >= 0; } catch (_) { return false; }
  }
  var H = {
    nav: function (p) {
      st.urls[p.id] = p.url;
      if (!known(p.id)) return;
      if (typeof window.pushTabHist === 'function') window.pushTabHist(p.url, p.id);
      if (p.id === st.tabId && typeof window.shelterRecordVisit === 'function') window.shelterRecordVisit(p.url);
    },
    title: function (p) {
      if (known(p.id) && typeof window.shelterUpdateTitle === 'function') window.shelterUpdateTitle(p.id, p.title);
    },
    loading: function (p) {
      st.loading[p.id] = !!p.loading;
      if (p.id === st.tabId && typeof window.onNativeLoading === 'function') window.onNativeLoading(!!p.loading);
    },
    newtab: function (p) {
      if (p.url && typeof window.newTab === 'function') window.newTab(p.url);
    },
    key: function (p) {
      var ev = new KeyboardEvent('keydown', {
        code: p.code, key: p.key || '', ctrlKey: !!p.ctrl, metaKey: !!p.meta,
        shiftKey: !!p.shift, altKey: !!p.alt, bubbles: true, cancelable: true
      });
      (document.activeElement || document.body).dispatchEvent(ev);
    },
    ctx: function (p) {
      if (typeof window.shelterCtxMenu !== 'function') return;
      var vp = vpRect() || { x: 0, y: 0 };
      var id = p.id;
      var wv = {
        reload: function () { q('view.act', { id: id, act: 'reload' }); },
        print: function () { q('view.act', { id: id, act: 'print' }); }
      };
      window.shelterCtxMenu({
        params: p.params || {}, wv: wv, gid: id, partition: p.partition,
        point: { x: vp.x + (p.x || 0), y: vp.y + (p.y || 0) }
      });
    },
    found: function (p) {
      if (p.id !== st.tabId || !st.visible) return;
      var c = byId('findCnt'), i = byId('findInput');
      if (!c || !i || !i.value.trim()) return;
      c.textContent = p.count > 0 ? (Math.max(1, p.idx) + ' / ' + p.count) : '0 / 0';
    },
    dlprompt: function (p) { if (cbs.dlprompt) cbs.dlprompt(p); },
    download: function (p) { if (typeof window.shelterDownload === 'function') window.shelterDownload(p); }
  };
  window.__shelterHost = {
    ev: function (name, payload) {
      var f = H[name];
      if (!f) return;
      try { f(payload || {}); } catch (e) { try { console.error('[shelter]', name, e); } catch (_) {} }
    }
  };

  /* ---------- перехват «Обновить» / F5 для нативных страниц ---------- */
  document.addEventListener('click', function (e) {
    if (!st.visible) return;
    var b = e.target && e.target.closest ? e.target.closest('[data-act="reload"]') : null;
    if (!b) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    q('view.act', { id: st.tabId, act: 'reload' });
  }, true);
  document.addEventListener('keydown', function (e) {
    if (!st.visible) return;
    var mod = e.metaKey || e.ctrlKey;
    if (e.code === 'F5' || (mod && !e.shiftKey && !e.altKey && e.code === 'KeyR')) {
      e.preventDefault();
      e.stopImmediatePropagation();
      q('view.act', { id: st.tabId, act: e.shiftKey ? 'reloadHard' : 'reload' });
    }
  }, true);

    /* ---------- поиск по странице нативной вкладки ---------- */
  var findTimer = 0;
  function nativeFind(next, forward) {
    var i = byId('findInput');
    if (!i) return;
    q('find', { id: st.tabId, q: i.value, next: !!next, forward: forward !== false });
    var c = byId('findCnt');
    if (c && !i.value.trim()) c.textContent = '';
  }
  document.addEventListener('input', function (e) {
    if (!st.visible || !e.target || e.target.id !== 'findInput') return;
    e.stopImmediatePropagation();
    clearTimeout(findTimer);
    findTimer = setTimeout(function () { nativeFind(false, true); }, 120);
  }, true);
  document.addEventListener('keydown', function (e) {
    if (!st.visible || !e.target || e.target.id !== 'findInput' || e.key !== 'Enter') return;
    e.preventDefault();
    e.stopImmediatePropagation();
    nativeFind(true, !e.shiftKey);
  }, true);
  document.addEventListener('click', function (e) {
    if (!st.visible || !e.target || !e.target.closest) return;
    var b = e.target.closest('[data-act="findPrev"],[data-act="findNext"]');
    if (!b) return;
    e.preventDefault();
    e.stopImmediatePropagation();
    nativeFind(true, b.getAttribute('data-act') === 'findNext');
  }, true);
  var lastFindHidden = true;
  function watchFindbar() {
    var fb = byId('findbar');
    if (!fb) return;
    lastFindHidden = fb.hidden;
    new MutationObserver(function () {
      if (fb.hidden === lastFindHidden) return;
      lastFindHidden = fb.hidden;
      st.lastRect = '';
      if (fb.hidden) q('find', { id: st.tabId, act: 'stop' });
      else if (st.visible) { var i = byId('findInput'); if (i && i.value.trim()) nativeFind(false, true); }
      relayout();
    }).observe(fb, { attributes: true, attributeFilter: ['hidden'] });
  }

  /* ---------- наблюдатели ---------- */
  function startObservers() {
    watchFindbar();
    var vp = byId('viewport');
    if (window.ResizeObserver && vp) new ResizeObserver(relayout).observe(vp);
    window.addEventListener('resize', relayout);
    document.addEventListener('transitionend', relayout, true);
    document.addEventListener('animationend', relayout, true);
    new MutationObserver(function () { scheduleEval(); relayout(); }).observe(document.body, {
      childList: true, subtree: true, attributes: true, attributeFilter: ['hidden', 'class']
    });
    var tl = byId('tabList');
    if (tl) new MutationObserver(scheduleSync).observe(tl, { childList: true, subtree: true });
    setInterval(syncTabs, 4000);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', startObservers);
  else startObservers();
})();
