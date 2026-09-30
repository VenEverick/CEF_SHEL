#!/usr/bin/env python3
"""Дымовой тест SHELTER в CI: запускает приложение, подключается по CDP, проверяет UI,
мост и открытие вкладки, делает скриншоты (через CDP и, где возможно, экрана ОС)."""
import base64, json, os, subprocess, sys, time, urllib.request, platform

import websocket  # pip install websocket-client

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


args = [exe, f"--remote-debugging-port={PORT}", "--remote-allow-origins=*"]
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
    log("SMOKE DONE")
finally:
    try:
        proc.terminate()
    except Exception:
        pass
    open(f"{out}/smoke.log", "w", encoding="utf-8").write("\n".join(log_lines))
