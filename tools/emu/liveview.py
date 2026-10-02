#!/usr/bin/env python3
"""Live view for the headless Firebird emulator.

Serves a page that shows the calculator's LCD as it runs and forwards clicks on
the on-screen keypad to the emulator, so Muntcraft can be played from a browser.

    python3 tools/emu/liveview.py --boot1 ~/nspire-kit/boot1.img \
                                 --flash ~/nspire-kit/work-flash

Environment:
    PORT         port to listen on (default 8000)
    MUNT_EMU     path to munt-headless (default ~/firebird/munt-headless)
    MUNT_LIVE_DIR  where frames are written (default /tmp/munt-live)

The emulator is started with --live and --input-dir, which is all the server
needs: it reads the frame PNG the emulator keeps overwriting and drops *.cmd
files into the input directory for key presses.
"""

import argparse
import os
import subprocess
import sys
import threading
import time
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FRAME_DIR = os.environ.get("MUNT_LIVE_DIR", "/tmp/munt-live")
FRAME_PATH = os.path.join(FRAME_DIR, "live.png")
INPUT_DIR = os.path.join(FRAME_DIR, "input")

# Keypad layout, in the order it appears in the page.
KEY_ROWS = [
    ("esc", "tab", "menu", "doc"),
    ("enter", "del", "cat", "var"),
]
ARROWS = [
    ("", "up", ""),
    ("left", "click", "right"),
    ("", "down", ""),
]
HOLDABLE = ("up", "down", "left", "right")  # touchpad arrows repeat while held

TYPING = {i: i for i in "abcdefghijklmnopqrstuvwxyz0123456789"}
TYPING.update({" ": "space", ".": "dot", "-": "minus", "+": "plus"})

state = {
    "status": "starting",
    "fps": 0.0,
    "proc": None,
    "log": [],
    "emu_cmd": "",
}


def log(line):
    state["log"].append(line.rstrip())
    del state["log"][:-60]


def run_emulator(args):
    """Keep the emulator running, restarting it whenever it exits."""
    while True:
        cmd = [args.emu,
               "--boot1", args.boot1,
               "--flash", args.flash,
               "--live", FRAME_PATH,
               "--live-fps", str(args.fps),
               "--input-dir", INPUT_DIR,
               "--realtime",
               "--keep-running"]
        for put in args.put:
            cmd += ["--put", put]
        if args.put:
            cmd += ["--put-at", str(args.put_at)]
        state["emu_cmd"] = " ".join(cmd)

        state["status"] = "running"
        log("$ " + state["emu_cmd"])
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                    text=True, bufsize=1, errors="replace")
        except OSError as exc:
            state["status"] = "cannot start emulator: %s" % exc
            log(state["status"])
            time.sleep(5)
            continue

        state["proc"] = proc
        for line in proc.stderr:
            log(line)
        proc.wait()
        state["status"] = "emulator exited (%s), restarting" % proc.returncode
        log(state["status"])
        time.sleep(2)


def monitor_frames():
    """Measure how fast the emulator is publishing frames."""
    last_mtime, count, last_time = 0.0, 0, time.time()
    while True:
        time.sleep(0.5)
        try:
            mtime = os.path.getmtime(FRAME_PATH)
        except OSError:
            mtime = 0.0
        if mtime != last_mtime:
            last_mtime = mtime
            count += 1
        now = time.time()
        if now - last_time >= 2.0:
            state["fps"] = count / (now - last_time)
            count, last_time = 0, now


def send_key(name, mode=""):
    os.makedirs(INPUT_DIR, exist_ok=True)
    stamp = "%013.3f-%d" % (time.time(), os.getpid())
    tmp = os.path.join(INPUT_DIR, ".%s.tmp" % stamp)
    path = os.path.join(INPUT_DIR, "%s.cmd" % stamp)
    # Written to a temporary name and renamed: the emulator scans the directory
    # continuously and would otherwise be able to open the file between its
    # creation and the write, reading an empty command and dropping the key.
    with open(tmp, "w") as fh:
        fh.write(mode + name)
    os.replace(tmp, path)


def buttons(names, hold=False, cls=""):
    out = []
    for name in names:
        if not name:
            out.append('<span style="min-width:62px"></span>')
            continue
        extra = ' class="%s"' % cls if cls else ""
        out.append('<button%s onclick="send(\'%s\')">%s</button>' % (extra, name, name))
    return "".join(out)


def render_page():
    rows = "".join('<div class="row">%s</div>' % buttons(row) for row in KEY_ROWS)
    pad = "".join('<div class="row">%s</div>' % buttons(row) for row in ARROWS)
    buttons_js = "".join(
        '<button onclick="send(\'%s\')">%s</button>' % (k, k) for k in TYPING if len(k) == 1)
    return PAGE % {"keys": rows, "pad": pad, "chars": buttons_js}


PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>Muntcraft - TI-Nspire CX live</title>
<style>
 body { background:#16181d; color:#dfe3ea; font:14px/1.4 system-ui,sans-serif;
        margin:0; display:flex; flex-direction:column; align-items:center; }
 h1 { font-size:15px; font-weight:600; margin:14px 0 4px; letter-spacing:.4px; }
 #hint { color:#8b93a1; font-size:12px; margin-bottom:10px; }
 #screen { image-rendering:pixelated; width:640px; height:480px;
           border:10px solid #2a2e37; border-radius:10px; background:#000; }
 .row { display:flex; gap:6px; margin:4px 0; justify-content:center; flex-wrap:wrap; }
 button { min-width:62px; padding:9px 12px; font:12px system-ui,sans-serif;
          color:#dfe3ea; background:#262b35; border:1px solid #3a4150;
          border-radius:6px; cursor:pointer; }
 button:hover { background:#333a47; } button:active { background:#3f6ea8; }
 #stats { color:#6f7787; font-size:11px; margin:12px 0 28px;
          font-family:ui-monospace,monospace; }
 input[type=text] { padding:8px; background:#262b35; color:#dfe3ea;
                    border:1px solid #3a4150; border-radius:6px; width:160px; }
</style></head><body>
<h1>Muntcraft on a TI-Nspire CX</h1>
<div id="hint">Live from the emulator. Click the keys to drive it; type text and click Send.</div>
<img id="screen" src="/frame.png" alt="calculator screen">
<div class="row">%(keys)s</div>
<div class="row">%(pad)s</div>
<div class="row"><input id="text" type="text" maxlength="16" placeholder="text for the keypad">
<button onclick="sendText()">Send</button></div>
<div class="row">%(chars)s</div>
<div id="stats">connecting...</div>
<script>
const img = document.getElementById('screen');
const stats = document.getElementById('stats');
setInterval(() => { img.src = '/frame.png?t=' + Date.now(); }, 120);
setInterval(async () => {
  try {
    const r = await fetch('/status');
    const s = await r.json();
    stats.textContent = 'frame age ' + s.age.toFixed(1) + 's  ' + s.fps.toFixed(1) +
                        ' fps  ' + s.status;
  } catch (e) { stats.textContent = 'server unreachable'; }
}, 1000);
function send(name) { fetch('/key?name=' + encodeURIComponent(name)).catch(() => {}); }
function sendText() {
  const el = document.getElementById('text');
  const chars = el.value.toLowerCase();
  el.value = '';
  [...chars].forEach((ch, i) => {
    const name = ch === ' ' ? 'space' : (ch === '.' ? 'dot' : ch);
    setTimeout(() => send(name), i * 140);
  });
}
</script></body></html>
"""


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def respond(self, code, ctype, data, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if data:
            self.wfile.write(data)

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path in ("/", "/index.html"):
            self.respond(200, "text/html; charset=utf-8", render_page().encode())
        elif path == "/frame.png":
            try:
                with open(FRAME_PATH, "rb") as fh:
                    data = fh.read()
            except OSError:
                data = b""
            self.respond(200 if data else 503, "image/png", data,
                         {"Cache-Control": "no-store"})
        elif path == "/status":
            try:
                age = time.time() - os.path.getmtime(FRAME_PATH)
            except OSError:
                age = -1.0
            body = ('{"age": %.2f, "fps": %.1f, "status": "%s"}' %
                    (age, state["fps"], state["status"].replace('"', "'"))).encode()
            self.respond(200, "application/json", body, {"Cache-Control": "no-store"})
        elif path == "/log":
            self.respond(200, "text/plain; charset=utf-8",
                         "\n".join(state["log"][-40:]).encode())
        elif path == "/key":
            params = urllib.parse.parse_qs(parsed.query)
            name = params.get("name", [""])[0]
            mode = params.get("mode", [""])[0]
            if not name:
                self.respond(400, "text/plain", b"missing name")
                return
            if name[0] in "+-":
                mode, name = name[0], name[1:]
            send_key(name, mode)
            self.respond(200, "text/plain", b"ok")
        else:
            self.respond(404, "text/plain", b"not found")


def main():
    parser = argparse.ArgumentParser(description="Live view for the TI-Nspire emulator")
    parser.add_argument("--boot1", default=os.environ.get("MUNT_BOOT1",
                                                          os.path.expanduser("~/nspire-kit/boot1.img")))
    parser.add_argument("--flash", default=os.environ.get("MUNT_FLASH",
                                                          os.path.expanduser("~/nspire-kit/work-flash")))
    parser.add_argument("--emu", default=os.environ.get("MUNT_EMU",
                                                        os.path.expanduser("~/firebird/munt-headless")))
    parser.add_argument("--fps", type=float, default=8.0, help="emulated LCD frames per second")
    parser.add_argument("--put", action="append", default=[],
                        help="local:remote file to send after boot (repeatable)")
    parser.add_argument("--put-at", type=float, default=25.0)
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", 8000)))
    parser.add_argument("--only-serve", action="store_true",
                        help="serve frames without starting an emulator")
    args = parser.parse_args()

    os.makedirs(FRAME_DIR, exist_ok=True)
    os.makedirs(INPUT_DIR, exist_ok=True)

    if not args.only_serve:
        if not os.path.exists(args.emu):
            sys.exit("munt-headless not found at %s - run tools/emu/setup_firebird.sh" % args.emu)
        if not os.path.exists(args.flash):
            sys.exit("flash image not found at %s" % args.flash)

    if not args.only_serve:
        threading.Thread(target=run_emulator, args=(args,), daemon=True).start()
    threading.Thread(target=monitor_frames, daemon=True).start()

    try:
        server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    except OSError as exc:
        sys.exit("cannot bind port %s: %s" % (args.port, exc))

    print("live view on http://0.0.0.0:%d/ (frames in %s)" % (args.port, FRAME_DIR), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
