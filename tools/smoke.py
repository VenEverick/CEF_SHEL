#!/usr/bin/env python3
"""Дымовой тест SHELTER в CI: запускает приложение, подключается по CDP, проверяет UI,
мост и открытие вкладки, делает скриншоты (через CDP и, где возможно, экрана ОС)."""
import base64, json, os, subprocess, sys, time, urllib.request, platform

import websocket  # pip install websocket-client

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

exe, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
PORT = 9222
log_lines = []


def log(*a):
    s = " ".join(str(x) for x in a)
    print(s, flush=True)
    log_lines.append(s)


def targets():
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json/list", timeout=3) as r:
            return json.load(r)
    except Exception as e:
        return []


class Cdp:
    def __init__(self, url):
        self.ws = websocket.create_connection(url, timeout=20, suppress_origin=True)
        self.n = 0
        self.events = []

    def call(self, method, params=None, timeout=20):
        self.n += 1
        mid = self.n
        self.ws.send(json.dumps({"id": mid, "method": method, "params": params or {}}))
        end = time.time() + timeout
        while time.time() < end:
            try:
                m = json.loads(self.ws.recv())
            except Exception:
                break
            if m.get("id") == mid:
                return m
            self.events.append(m)
        return {"error": "timeout"}

    def eval(self, expr, await_promise=False):
        r = self.call("Runtime.evaluate", {"expression": expr, "returnByValue": True, "awaitPromise": await_promise})
        res = r.get("result", {})
        if "exceptionDetails" in res:
            return "EXC: " + json.dumps(res["exceptionDetails"])[:300]
        return res.get("result", {}).get("value")

    def drain(self, secs):
        end = time.time() + secs
        self.ws.settimeout(0.5)
        while time.time() < end:
            try:
                self.events.append(json.loads(self.ws.recv()))
            except Exception:
                pass
        self.ws.settimeout(20)

    def shot(self, path):
        r = self.call("Page.captureScreenshot", {"format": "png"})
        data = r.get("result", {}).get("data")
        if data:
            open(path, "wb").write(base64.b64decode(data))
        return bool(data)


def os_shot(path):
    path = os.path.abspath(path)
    try:
        if platform.system() == "Windows":
            ps = ("Add-Type -AssemblyName System.Windows.Forms,System.Drawing;"
                  "$b=[System.Windows.Forms.SystemInformation]::VirtualScreen;"
                  "$bmp=New-Object System.Drawing.Bitmap $b.Width,$b.Height;"
                  "$g=[System.Drawing.Graphics]::FromImage($bmp);"
                  "$g.CopyFromScreen($b.Location,[System.Drawing.Point]::Empty,$b.Size);"
                  f"$bmp.Save('{path}')")
            subprocess.run(["powershell", "-NoProfile", "-Command", ps], timeout=60)
        else:
            subprocess.run(["screencapture", "-x", path], timeout=60)
    except Exception as e:
        log("os_shot failed:", e)


args = [exe, f"--remote-debugging-port={PORT}", "--remote-allow-origins=*"] + sys.argv[3:]
log("launch:", args)
proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    ui = None
    for _ in range(60):
        time.sleep(1)
        ts = targets()
        ui = next((t for t in ts if t.get("url", "").startswith("shelter://app/")), None)
        if ui:
            break
    log("targets:", json.dumps([(t.get("type"), t.get("url"), t.get("title")) for t in targets()], ensure_ascii=False))
    if not ui:
        log("FAIL: UI target not found; process alive =", proc.poll() is None)
        sys.exit(1)
    time.sleep(3)
    c = Cdp(ui["webSocketDebuggerUrl"])
    c.call("Runtime.enable")
    c.call("Log.enable")
    c.call("Page.enable")
    log("title:", c.eval("document.title"))
    log("shelterNative:", c.eval("JSON.stringify({n: !!window.shelterNative, p: window.shelterNative && window.shelterNative.platform, open: typeof window.shelterOpen, tabHook: typeof window.__shelterTab, cq: typeof window.cefQuery})"))
    log("viewport:", c.eval("JSON.stringify(document.getElementById('viewport').getBoundingClientRect())"))
    log("clip roundtrip:", c.eval("window.shelterNative.clipWrite('shelter-test').then(()=>window.shelterNative.clipRead())", True))
    c.shot(f"{out}/ui-start.png")
    os_shot(f"{out}/os-start.png")

    import shutil
    def active_url():
        return c.eval("(function(){var t=(window.getSpaceTabs()||[]).find(function(t){return t.id===window.getActiveTabId()});return t&&t.url})()")
    def real_click(expr, label):
        r = c.eval("(function(){var e=(%s); if(!e) return null; var b=e.getBoundingClientRect();"
                   "return JSON.stringify({x:window.screenX+b.x+b.width/2,y:window.screenY+b.y+b.height/2,sx:window.screenX,sy:window.screenY,iw:window.innerWidth,ow:window.outerWidth})})()" % expr)
        if not r:
            log(f"real click {label}: element not found"); return
        d = json.loads(r)
        res = subprocess.run(["cliclick", f"c:{int(d['x'])},{int(d['y'])}"], capture_output=True, text=True)
        log(f"real click {label} at {int(d['x'])},{int(d['y'])} win=({d['sx']},{d['sy']}) iw={d['iw']} ow={d['ow']} rc={res.returncode} {res.stderr.strip()[:100]}")
        time.sleep(2.5)
        log(f"   -> active tab url: {active_url()}")
    if shutil.which("cliclick"):
        log("--- control: real click on bare UI (no native view) ---")
        real_click("document.querySelector('.ni[data-page=bookmarks]')", "sidebar bookmarks (control)")
        real_click("document.querySelector('.ni[data-page=start]')", "sidebar start (control)")

    # новая вкладка с реальным сайтом
    log("newTab:", c.eval("window.newTab('https://example.org/')"))
    time.sleep(8)
    ts = targets()
    log("targets after newTab:", json.dumps([(t.get("type"), t.get("url"), t.get("title")) for t in ts], ensure_ascii=False))
    log("active tab url in UI:", c.eval("(window.getSpaceTabs()||[]).map(t=>t.url+' | '+t.title).join(' ;; ')"))
    os_shot(f"{out}/os-tab.png")
    c.shot(f"{out}/ui-tab.png")

    # страница во вкладке
    pg = next((t for t in ts if t.get("type") == "page" and "example.org" in t.get("url", "")), None)
    if pg:
        p = Cdp(pg["webSocketDebuggerUrl"])
        log("tab title:", p.eval("document.title"), "| shelterNative in tab:", p.eval("typeof window.shelterNative"),
            "| cefQuery in tab:", p.eval("typeof window.cefQuery"))
        p.shot(f"{out}/tab-page.png")
    else:
        log("WARN: example.org tab target not found")

    # меню поверх нативной вкладки -> снимок
    log("open menu:", c.eval("(function(){var b=document.querySelector('[data-act=\"more\"]'); if(!b) return 'no-more-btn'; b.click(); return 'clicked';})()"))
    time.sleep(2.5)
    log("snap img present:", c.eval("!!document.getElementById('shSnap')"),
        "| menu:", c.eval("!!document.querySelector('.menu:not(.closing)')"))
    os_shot(f"{out}/os-menu.png")
    c.call("Input.dispatchKeyEvent", {"type": "keyDown", "key": "Escape", "code": "Escape", "windowsVirtualKeyCode": 27})
    c.call("Input.dispatchKeyEvent", {"type": "keyUp", "key": "Escape", "code": "Escape", "windowsVirtualKeyCode": 27})
    time.sleep(2)
    log("after close: snap img =", c.eval("!!document.getElementById('shSnap')"))
    os_shot(f"{out}/os-after-menu.png")

    # сценарий «поиск -> капча Google»: UI должен оставаться живым
    def ui_state(tag):
        t0 = time.time()
        v = c.eval("JSON.stringify({url:(window.getSpaceTabs()||[]).map(t=>t.url+' | '+t.title).join(' ;; '),"
                   "ref:(document.getElementById('btnRef')||{dataset:{}}).dataset.act,"
                   "back:document.getElementById('btnBack').className,"
                   "omni:(document.getElementById('omniInput')||{}).value,"
                   "snap:!!document.getElementById('shSnap'),"
                   "vp:(function(){var r=document.getElementById('viewport').getBoundingClientRect();return [r.x,r.y,r.width,r.height].map(Math.round)})()})")
        log(f"[{tag}] ui ({time.time()-t0:.2f}s):", v)
    log("navigate CC:", c.eval("window.navigate('CC')"))
    for i in range(4):
        time.sleep(4)
        ui_state(f"search+{(i+1)*4}s")
    ts = targets()
    log("targets after search:", json.dumps([(t.get("type"), t.get("url")[:110], t.get("title")) for t in ts], ensure_ascii=False))
    os_shot(f"{out}/os-search.png")
    c.shot(f"{out}/ui-search.png")
    # настоящие клики мышью ОС (cliclick, только macOS): проверяем, что UI реагирует на ввод,
    # когда поверх лежит нативный вид вкладки
    if shutil.which("cliclick"):
        log("--- real mouse test ---")
        log("active before:", active_url())
        real_click("document.querySelector('.ni[data-page=bookmarks]')", "sidebar bookmarks")
        real_click("Array.from(document.querySelectorAll('#tabList .tab[data-tab]')).find(function(e){return /Google/.test(e.textContent)})", "sidebar google tab")
        os_shot(f"{out}/os-real1.png")
        real_click("document.getElementById('viewport')", "page area")
        real_click("document.querySelector('.ni[data-page=history]')", "sidebar history after page click")
        os_shot(f"{out}/os-real2.png")
        real_click("document.getElementById('btnBack')", "back button")
        os_shot(f"{out}/os-real3.png")
    else:
        log("cliclick not available, real mouse test skipped")
    if shutil.which("cliclick"):
        log("--- hit-test diagnostics ---")
        ts2 = targets()
        gp = next((t for t in ts2 if t.get("type") == "page" and "google.com" in t.get("url", "")), None)
        pc = Cdp(gp["webSocketDebuggerUrl"]) if gp else None
        inj = "window.__clicks=[];['mousemove','mousedown','pointerdown','click'].forEach(function(n){addEventListener(n,function(e){window.__clicks.push(n+'@'+Math.round(e.clientX)+','+Math.round(e.clientY))},true)});'ok'"
        log("inject ui:", c.eval(inj))
        if pc: log("inject page:", pc.eval(inj))
        def probe(label, expr):
            r = c.eval("(function(){var e=(%s); var b=e.getBoundingClientRect(); return JSON.stringify({x:window.screenX+b.x+b.width/2,y:window.screenY+b.y+b.height/2})})()" % expr)
            d = json.loads(r)
            c.eval("window.__clicks=[]");
            if pc: pc.eval("window.__clicks=[]")
            subprocess.run(["cliclick", f"c:{int(d['x'])},{int(d['y'])}"], capture_output=True, text=True)
            time.sleep(1.5)
            log(f"probe {label} @{int(d['x'])},{int(d['y'])}: ui={c.eval('JSON.stringify(window.__clicks)')} page={pc.eval('JSON.stringify(window.__clicks)') if pc else None} uiFocus={c.eval('document.hasFocus()')}")
        log("app-regions:", c.eval("""(function(){var out=[];document.querySelectorAll('body, body *').forEach(function(e){var v=getComputedStyle(e).webkitAppRegion; if(v&&v!=='none'){var b=e.getBoundingClientRect(); out.push((e.id||e.className||e.tagName).toString().slice(0,30)+':'+v+':'+[b.x,b.y,b.width,b.height].map(Math.round).join(','))}}); return JSON.stringify(out.slice(0,60))})()"""))
        probe("sidebar bookmarks", "document.querySelector('.ni[data-page=bookmarks]')")
        probe("toolbar back", "document.getElementById('btnBack')")
        probe("omnibox", "document.getElementById('omniInput')")
        probe("viewport center", "document.getElementById('viewport')")
        probe("sidebar settings", "document.querySelector('[data-act=settings]')||document.querySelector('.sb-foot button')")
        probe("sidebar bookmarks again", "document.querySelector('.ni[data-page=bookmarks]')")
        os_shot(f"{out}/os-probe.png")
        try:
            import Quartz
            wl = Quartz.CGWindowListCopyWindowInfo(Quartz.kCGWindowListOptionAll, Quartz.kCGNullWindowID)
            for w in wl:
                if w.get("kCGWindowOwnerPID") == proc.pid:
                    b = w.get("kCGWindowBounds", {})
                    log("window:", w.get("kCGWindowNumber"), "layer", w.get("kCGWindowLayer"), "alpha", w.get("kCGWindowAlpha"),
                        "onscreen", w.get("kCGWindowIsOnscreen"), "name", repr(w.get("kCGWindowName")),
                        "bounds", int(b.get("X", 0)), int(b.get("Y", 0)), int(b.get("Width", 0)), int(b.get("Height", 0)))
            log("window list total:", len(wl), "pid:", proc.pid)
        except Exception as e:
            log("window list unavailable:", e)
    log("click sidebar history:", c.eval("(function(){var b=document.querySelector('.ni[data-page=history]'); if(!b) return 'none'; b.click(); return 'clicked';})()"))
    time.sleep(2)
    ui_state("after history click")
    os_shot(f"{out}/os-history.png")
    log("click back:", c.eval("(function(){document.getElementById('btnBack').click();return 'clicked'})()"))
    time.sleep(3)
    ui_state("after back")
    os_shot(f"{out}/os-back.png")

    # ошибки консоли UI
    c.drain(1)
    errs = []
    for e in c.events:
        if e.get("method") == "Runtime.exceptionThrown":
            errs.append(json.dumps(e["params"]["exceptionDetails"], ensure_ascii=False)[:300])
        if e.get("method") == "Log.entryAdded" and e["params"]["entry"].get("level") in ("error", "warning"):
            errs.append(e["params"]["entry"].get("text", "")[:200] + " " + e["params"]["entry"].get("url", ""))
    log("ui console problems:", len(errs))
    for e in errs[:15]:
        log("  ", e)
    try:
        lp = os.path.expanduser("~/Library/Application Support/SHELTER/layout.log") if platform.system() == "Darwin" else os.path.join(os.environ.get("LOCALAPPDATA", ""), "SHELTER", "layout.log")
        log("--- layout.log ---")
        log(open(lp, encoding="utf-8", errors="replace").read()[-6000:])
    except Exception as e:
        log("layout.log unavailable:", e)
    log("SMOKE DONE")
finally:
    try:
        proc.terminate()
    except Exception:
        pass
    open(f"{out}/smoke.log", "w", encoding="utf-8").write("\n".join(log_lines))
