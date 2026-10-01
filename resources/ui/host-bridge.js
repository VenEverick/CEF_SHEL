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
    '.sh-snap-wait .menu,.sh-snap-wait .scrim,.sh-snap-wait #toasts,.sh-snap-wait .suggest{visibility:hidden!important}' +
    '#shSnap{position:absolute;left:0;top:0;width:100%;height:100%;object-fit:fill;pointer-events:none;z-index:3;background:#fff}' +
    (PLATFORM === 'darwin' ? '#winLights{visibility:hidden!important}' : '');
  (document.head || root).appendChild(style);
  root.classList.add('host-' + PLATFORM);

  /* ---------- снимок вместо нативного вида ---------- */
  var OVERLAY_SEL = '.menu:not(.closing),.scrim:not(.closing),#toasts>.toast:not(.out),' +
    '.suggest:not([hidden]),#findbar:not([hidden]),#dlFloat:not([hidden])';

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
    var id = st.tabId;
    root.classList.add('sh-snap-wait');
    qSnap(id, 78, 900).then(function (url) {
      if (url && st.tabId === id) { st.snapUrl = url; st.snapFor = id; showSnap(url); }
      return nextFrames(2);
    }).then(function () {
      st.frozen = true;
      return q('view.hide');
    }).then(function () {
      st.busy = false;
      root.classList.remove('sh-snap-wait');
      evaluate();
    });
  }
  function thaw() {
    st.busy = true;
    var r = vpRect();
    q('view.show', { rect: r, focus: !editableFocused() }).then(function () { return nextFrames(2); }).then(function () {
      st.frozen = false;
      dropSnap();
      st.busy = false;
      evaluate();
    });
  }
  function evaluate() {
    if (st.busy) return;
    var need = st.visible && overlayOverViewport();
    if (need && !st.frozen) freeze();
    else if (!need && st.frozen && st.visible) thaw();
    else if (!st.visible && st.frozen) { st.frozen = false; dropSnap(); }
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
    var r = vpRect();
    var k = rectKey(r);
    if (!r || k === st.lastRect || r.w < 2) return;
    st.lastRect = k;
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
    downloadDecision: function (o) { return q('dl.decision', o || {}); }
    // secretEnc / secretDec / authWindow / devTools — намеренно не определены (этап 2+):
    // вёрстка при их отсутствии использует безопасный запасной путь.
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
      c.textContent = p.count > 0 ? (p.idx + ' / ' + p.count) : '0 / 0';
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
