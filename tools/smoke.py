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


def unq(r):
    try:
        return json.loads(r)
    except Exception:
        return str(r)


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
    log("secret roundtrip:", c.eval("(function(){var n=window.shelterNative,e=n.secretEnc('Пароль-123');return [e.slice(0,4),n.secretDec(e)==='Пароль-123',n.secretDec(e.slice(0,-2)+'AA')===''].join('|')})()"))
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
        r0 = c.eval("new Promise(function(res){cefQuery({request:JSON.stringify({m:'dbg.hit',a:{x:130,y:332}}),onSuccess:function(r){res(r)},onFailure:function(c,m){res('fail '+m)}})})", True)
        log("hit-test control sidebar (130,332):\n" + unq(r0)[:4000])
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
    # поиск по странице (нативный, Find) на открытой выдаче Google
    log("--- find in page ---")
    c.eval("window.openFind()")
    time.sleep(0.6)
    c.eval("(function(){var i=document.getElementById('findInput');i.value='Creative';i.dispatchEvent(new Event('input',{bubbles:true}));})()")
    time.sleep(2.5)
    log("find cnt:", c.eval("document.getElementById('findCnt').textContent"),
        "| snap:", c.eval("!!document.getElementById('shSnap')"))
    c.eval("document.querySelector('[data-act=findNext]').click()")
    time.sleep(1)
    log("find cnt after next:", c.eval("document.getElementById('findCnt').textContent"))
    os_shot(f"{out}/os-find.png")
    c.eval("document.querySelector('[data-act=findClose]').click()")
    time.sleep(1)
    log("find closed, hidden:", c.eval("document.getElementById('findbar').hidden"))
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
        for lab, (hx, hy) in (("sidebar", (130, 332)), ("viewport", (850, 467))):
            r = c.eval("new Promise(function(res){cefQuery({request:JSON.stringify({m:'dbg.hit',a:{x:%d,y:%d}}),onSuccess:function(r){res(r)},onFailure:function(c,m){res('fail '+m)}})})" % (hx, hy), True)
            log(f"hit-test {lab} ({hx},{hy}):\n" + unq(r)[:4000])
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

    # функциональный сценарий на локальном сервере: навигация, назад/вперёд, загрузка, зум, закрытие вкладки
    log("--- functional scenario (local server) ---")
    import threading, http.server, socketserver
    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a): pass
        def do_GET(self):
            if self.path.startswith("/file.bin"):
                body = b"S" * 200000
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Disposition", 'attachment; filename="shelter-test.bin"')
                self.send_header("Content-Length", str(len(body)))
                self.end_headers(); self.wfile.write(body); return
            if self.path.startswith("/mark"):
                m = self.path.split("m=")[-1].split("&")[0]
                body = ("<!doctype html><title>Mark</title><h1>%s</h1><script>try{localStorage.setItem('k_%s','1')}catch(e){}</script>" % (m, m)).encode()
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.send_header("Cache-Control", "public, max-age=3600")
                self.send_header("Set-Cookie", "c_%s=1; Max-Age=86400; Path=/" % m)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers(); self.wfile.write(body); return
            if self.path.startswith("/dl"):
                body = b"<!doctype html><title>DL</title><a id=a1 href=/file.bin style='display:block;margin:40px;font-size:30px'>download-link</a><a id=a2 download=blob.txt href=# onclick=\"var b=new Blob(['blob-data-1234567890'],{type:'text/plain'});this.href=URL.createObjectURL(b)\" style='display:block;margin:40px;font-size:30px'>blob-link</a>"
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers(); self.wfile.write(body); return
            body = ("<!doctype html><title>Page %s</title><h1>Page %s</h1><p>needle needle needle</p>" % (self.path, self.path)).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
    socketserver.TCPServer.allow_reuse_address = True
    srv = socketserver.ThreadingTCPServer(("127.0.0.1", 8765), H)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    def tabs_desc():
        return c.eval("(window.getSpaceTabs()||[]).map(t=>(t.id===window.getActiveTabId()?'*':'')+t.url+' | '+t.title).join(' ;; ')")
    log("newTab p1:", c.eval("window.newTab('http://127.0.0.1:8765/p1')"))
    time.sleep(4)
    log("after p1:", tabs_desc())
    log("navigate p2:", c.eval("window.navigate('http://127.0.0.1:8765/p2')"))
    time.sleep(4)
    log("after p2:", tabs_desc())
    c.eval("document.getElementById('btnBack').click()")
    time.sleep(3)
    log("after back:", tabs_desc())
    c.eval("document.getElementById('btnFwd').click()")
    time.sleep(3)
    log("after forward:", tabs_desc())
    # поиск по тексту локальной страницы
    c.eval("window.openFind()")
    time.sleep(0.6)
    c.eval("(function(){var i=document.getElementById('findInput');i.value='needle';i.dispatchEvent(new Event('input',{bubbles:true}));})()")
    time.sleep(2.5)
    log("local find cnt (expect 1 / 3):", c.eval("document.getElementById('findCnt').textContent"))
    c.eval("document.querySelector('[data-act=findClose]').click()")
    time.sleep(1)
    # зум
    log("zoom:", c.eval("(function(){window.shelterSetZoomAll(1.5);return 'set'})()"))
    time.sleep(1.5)
    pg2 = next((t for t in targets() if t.get("type") == "page" and "8765/p2" in t.get("url", "")), None)
    if pg2:
        log("page devicePixel/zoom check, innerWidth:", Cdp(pg2["webSocketDebuggerUrl"]).eval("window.innerWidth"))
    c.eval("window.shelterSetZoomAll(1)")
    # загрузка
    dl_dir = os.path.join(os.path.expanduser("~"), "Downloads")
    target = os.path.join(dl_dir, "shelter-test.bin")
    if os.path.exists(target): os.remove(target)
    log("navigate download:", c.eval("window.navigate('http://127.0.0.1:8765/file.bin')"))
    time.sleep(4)
    log("download prompt shown:", c.eval("!!document.querySelector('[data-dlp=save]')"))
    c.eval("(function(){var b=document.querySelector('[data-dlp=save]'); if(b) b.click();})()")
    time.sleep(4)
    log("download file exists:", os.path.exists(target), os.path.getsize(target) if os.path.exists(target) else -1, "dir:", dl_dir)
    log("tabs after download:", tabs_desc())
    # --- масштаб при переходе на другой хост (Chromium хранит зум «по хосту») ---
    log("--- zoom across origins ---")
    def page_eval(sub, expr):
        pgx = next((t for t in targets() if t.get("type") == "page" and sub in t.get("url", "")), None)
        if not pgx: return None
        try:
            cc = Cdp(pgx["webSocketDebuggerUrl"]); r = cc.eval(expr); cc.ws.close(); return r
        except Exception as e:
            return "ERR %s" % e
    log("zoom: base innerWidth p2:", page_eval("127.0.0.1:8765/p2", "window.innerWidth"))
    c.eval("window.shelterSetZoomAll(1.5)"); time.sleep(1.5)
    log("zoom 1.5 same host p2:", page_eval("127.0.0.1:8765/p2", "window.innerWidth"))
    c.eval("window.navigate('http://localhost:8765/p3')"); time.sleep(4)
    log("zoom 1.5 other host p3 (expect ~base/1.5):", page_eval("localhost:8765/p3", "window.innerWidth"))
    c.eval("window.navigate('http://127.0.0.1:8765/p4')"); time.sleep(4)
    log("zoom 1.5 back to first host p4 (expect ~base/1.5):", page_eval("127.0.0.1:8765/p4", "window.innerWidth"))
    c.eval("window.shelterSetZoomAll(1)"); time.sleep(1)
    log("zoom reset p4:", page_eval("127.0.0.1:8765/p4", "window.innerWidth"))

    # --- «дыры» вместо снимков: подсказка, уведомление, подсказки омнибокса над страницей ---
    log("--- overlays over live page (holes) ---")
    log("perf before:", c.eval("JSON.stringify(window.__shPerf)"))
    c.eval("window.toast('Проверка уведомления над страницей',{icon:'info'})"); time.sleep(0.9)
    log("toast: snap =", c.eval("!!document.getElementById('shSnap')"), "| clip =", c.eval("window.__shClip()"))
    os_shot(f"{out}/os-toast.png")
    time.sleep(5)
    log("after toast: clip =", c.eval("window.__shClip()"))
    if shutil.which("cliclick"):
        d = json.loads(c.eval("(function(){var b=document.getElementById('btnBack').getBoundingClientRect();return JSON.stringify({x:window.screenX+b.x+b.width/2,y:window.screenY+b.y+b.height/2})})()"))
        subprocess.run(["cliclick", f"m:{int(d['x'])},{int(d['y'])}"])
        time.sleep(0.4)
        subprocess.run(["cliclick", f"m:{int(d['x'])+2},{int(d['y'])+1}"])
    else:
        c.eval("(function(){var b=document.getElementById('btnBack'); ['pointerover','mouseover','mouseenter','pointerenter'].forEach(function(n){b.dispatchEvent(new MouseEvent(n,{bubbles:true}))})})()")
    time.sleep(1.6)
    log("tip shown:", c.eval("!!document.querySelector('.tip')"), "| snap =", c.eval("!!document.getElementById('shSnap')"), "| clip =", c.eval("window.__shClip()"))
    os_shot(f"{out}/os-tip.png")
    # подсказки омнибокса
    c.eval("(function(){var i=document.getElementById('omniInput'); i.focus(); i.value='goo'; i.dispatchEvent(new Event('input',{bubbles:true}));})()")
    time.sleep(1.2)
    log("suggest shown:", c.eval("!!document.querySelector('#tbSuggest:not([hidden])')"), "| snap =", c.eval("!!document.getElementById('shSnap')"), "| clip =", c.eval("window.__shClip()"))
    os_shot(f"{out}/os-suggest.png")
    if shutil.which("cliclick"):
        before = active_url()
        real_click("document.querySelector('#tbSuggest > *:nth-child(2)') || document.querySelector('#tbSuggest > *')", "suggest item (click through hole)")
        log("suggest click changed url:", before != active_url(), "|", before, "->", active_url())
    c.eval("document.getElementById('omniInput').blur()")
    time.sleep(0.5)
    log("perf after:", c.eval("JSON.stringify(window.__shPerf)"))

    if platform.system() == "Windows":
        r_ = c.eval("new Promise(function(res){cefQuery({request:JSON.stringify({m:'dbg.clip',a:{}}),onSuccess:function(r){res(r)},onFailure:function(c,m){res('fail '+m)}})})", True)
        log("hwnd chain:\n" + str(unq(r_))[:3000])

    # --- скачивание по ссылке со страницы (жест пользователя, как на реальных сайтах) ---
    log("--- download via page link click ---")
    c.eval("window.navigate('http://127.0.0.1:8765/dl')"); time.sleep(4)
    pgd = next((t for t in targets() if t.get("type") == "page" and "8765/dl" in t.get("url", "")), None)
    if pgd:
        pd_ = Cdp(pgd["webSocketDebuggerUrl"])
        for lbl, sel in (("file link", "a1"), ("blob link", "a2")):
            if os.path.exists(target): os.remove(target)
            xy = json.loads(pd_.eval("(function(){var b=document.getElementById('%s').getBoundingClientRect();return JSON.stringify({x:b.x+20,y:b.y+b.height/2})})()" % sel))
            for ty in ("mouseMoved", "mousePressed", "mouseReleased"):
                pd_.call("Input.dispatchMouseEvent", {"type": ty, "x": xy["x"], "y": xy["y"], "button": "left", "clickCount": 1})
            time.sleep(3)
            log(f"{lbl}: prompt =", c.eval("!!document.querySelector('[data-dlp=save]')"))
            if shutil.which("cliclick"):
                real_click("document.querySelector('[data-dlp=save]')", f"{lbl}: real click on Save")
            else:
                c.eval("(function(){var b=document.querySelector('[data-dlp=save]'); if(b) b.click();})()")
            time.sleep(3)
            log(f"{lbl}: prompt still open =", c.eval("!!document.querySelector('[data-dlp=save]')"),
                "| file.bin exists =", os.path.exists(target), "| tabs:", tabs_desc())
            c.eval("(function(){var b=document.querySelector('[data-dlp=cancel]'); if(b) b.click();})()")
            time.sleep(1)
        pd_.ws.close()
    else:
        log("download page target not found")
    try:
        sl = os.path.expanduser("~/Library/Application Support/SHELTER/shelter.log") if platform.system() == "Darwin" else os.path.join(os.environ.get("LOCALAPPDATA", ""), "SHELTER", "shelter.log")
        log("--- shelter.log ---"); log(open(sl, encoding="utf-8", errors="replace").read()[-3000:])
    except Exception as e:
        log("shelter.log unavailable:", e)

    # закрытие вкладки
    n0 = c.eval("(window.getSpaceTabs()||[]).length")
    c.eval("window.closeTab(window.getActiveTabId())")
    time.sleep(2.5)
    log("tabs before/after close:", n0, c.eval("(window.getSpaceTabs()||[]).length"), "| active:", tabs_desc())
    os_shot(f"{out}/os-func.png")

    # --- «Призрак»: ничего не должно попасть на диск (контроль: обычное пространство) ---
    log("--- ghost: disk leak check ---")
    import uuid
    mp, mg = "PERSISTMARK" + uuid.uuid4().hex[:12], "GHOSTMARK" + uuid.uuid4().hex[:12]
    c.eval("window.newTab('http://127.0.0.1:8765/mark?m=%s')" % mp); time.sleep(4)
    c.eval("window.shelter.ghost()"); time.sleep(1.5)
    log("ghost on:", c.eval("localStorage.getItem('shelter:ghost')"))
    c.eval("window.newTab('http://127.0.0.1:8765/mark?m=%s')" % mg); time.sleep(5)
    log("ghost tab:", tabs_desc())
    log("page marker (ghost tab):", page_eval("mark?m=" + mg, "document.body.innerText"))
    log("ghost page cookies (must NOT contain PERSISTMARK):", page_eval("mark?m=" + mg, "document.cookie"))
    log("ghost page localStorage keys:", page_eval("mark?m=" + mg, "Object.keys(localStorage).join(',')"))
    log("persist page cookies:", page_eval("mark?m=" + mp, "document.cookie"))
    time.sleep(30)  # даём сбросить кеш/историю/localStorage на диск
    c.eval("window.shelterSetZoomAll(1)")
    udir = os.path.expanduser("~/Library/Application Support/SHELTER") if platform.system() == "Darwin" else os.path.join(os.environ.get("LOCALAPPDATA", ""), "SHELTER")
    hits = {mp: [], mg: []}
    nfiles = 0
    for root_, _, files_ in os.walk(udir):
        for fn in files_:
            fp = os.path.join(root_, fn)
            try:
                if os.path.getsize(fp) > 300 * 1024 * 1024: continue
                data = open(fp, "rb").read()
            except Exception:
                continue
            nfiles += 1
            for m_ in (mp, mg):
                if m_.encode() in data or m_.encode("utf-16le") in data:
                    hits[m_].append(os.path.relpath(fp, udir))
    log("files scanned:", nfiles, "in", udir)
    for base_ in (os.path.join(udir, "Profiles"), udir):
        for d_ in sorted(os.listdir(base_)) if os.path.isdir(base_) else []:
            fp_ = os.path.join(base_, d_)
            if os.path.isdir(fp_) and (base_ != udir or d_ in ("Default", "Profiles")):
                log("  dir", os.path.relpath(fp_, udir), [x for x in sorted(os.listdir(fp_))][:30])
    for sub_ in ("Cache", "Local Storage", "Session Storage", "Network"):
        for root_, dirs_, files_ in os.walk(os.path.join(udir, "Profiles", "space-main", sub_)):
            log("  ", os.path.relpath(root_, udir), len(files_), "files", sum(os.path.getsize(os.path.join(root_, f)) for f in files_))
    log("CONTROL (persistent space) marker found in:", hits[mp][:10] or "NOT FOUND")
    log("GHOST marker found on disk in:", hits[mg][:10] or "nothing (OK)")

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
