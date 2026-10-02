#!/usr/bin/env python3
"""Serve the frames a tour captured as a gallery.

    python3 tools/pcsim/gallery.py [--shots DIR] [--port PORT]

Runs after `sh tools/pcsim/play.sh`, which writes one PNG per shot plus a
manifest.txt of `<label>|<file>|<time>|<caption>` lines. Frames that share a
label are one clip and are played as an animation; a label with one frame is a
still. Nothing is computed here: the frames are the game's own output.

Environment:
    MUNT_SIM_SHOTS  where the frames are (default /tmp/munt-sim/shots)
    PORT            port to listen on (default 8000)
"""

import argparse
import json
import os
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DEFAULT_SHOTS = "/tmp/munt-sim/shots"


def load_manifest(shots):
    """The frames, in capture order, grouped into stills and clips."""
    try:
        with open(os.path.join(shots, "manifest.txt")) as fh:
            lines = [line.rstrip("\n") for line in fh if line.strip()]
    except OSError:
        return []

    entries = []
    for line in lines:
        parts = line.split("|")
        if len(parts) < 4:
            continue
        label, name, when, caption = parts[0], parts[1], parts[2], "|".join(parts[3:])
        if entries and entries[-1]["label"] == label:
            entries[-1]["frames"].append(name)
            continue
        entries.append({"label": label, "frames": [name], "time": when, "caption": caption})
    return entries


PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Muntcraft - autoplay tour</title>
<style>
 :root { color-scheme: dark; }
 * { box-sizing: border-box; }
 body { margin:0; background:#11151a; color:#dfe5ec;
        font:15px/1.55 system-ui,-apple-system,Segoe UI,sans-serif; }
 header { padding:38px 28px 22px; max-width:1120px; margin:0 auto; }
 h1 { margin:0 0 8px; font-size:29px; letter-spacing:-.4px; }
 h1 span { color:#8fb46a; }
 header p { margin:0; color:#96a0ad; max-width:760px; }
 header .meta { margin-top:14px; font:12px/1.6 ui-monospace,SFMono-Regular,monospace;
                color:#78828f; }
 header .meta a { color:#9ec27a; }
 main { max-width:1120px; margin:0 auto; padding:8px 28px 64px;
        display:grid; gap:26px; grid-template-columns:repeat(auto-fit,minmax(430px,1fr)); }
 figure { margin:0; background:#181d24; border:1px solid #242c35; border-radius:12px;
          overflow:hidden; display:flex; flex-direction:column; }
 .stage { position:relative; background:#000; aspect-ratio:4/3; }
 .stage img { width:100%%; height:100%%; display:block; image-rendering:pixelated; }
 .badge { position:absolute; top:10px; left:10px; background:#0d1116cc; color:#cfd7e0;
          font:11px/1 ui-monospace,monospace; padding:5px 8px; border-radius:99px;
          letter-spacing:.3px; }
 figcaption { padding:13px 15px 15px; display:flex; flex-direction:column; gap:8px; }
 h2 { margin:0; font-size:14px; letter-spacing:.5px; text-transform:uppercase; color:#9fb0c2; }
 figcaption p { margin:0; color:#cfd7e0; }
 .bar { display:flex; align-items:center; gap:10px; margin-top:2px; }
 .bar input { flex:1; accent-color:#8fb46a; }
 .bar button { font:12px ui-monospace,monospace; color:#dfe5ec; background:#232a33;
               border:1px solid #313b47; border-radius:6px; padding:5px 10px; cursor:pointer; }
 .bar button:hover { background:#2c3540; }
 .empty { max-width:640px; margin:0 auto; padding:60px 28px; }
 code { background:#1c222a; padding:2px 6px; border-radius:5px;
        font:13px ui-monospace,monospace; }
</style></head><body>
<header>
  <h1>Muntcraft <span>&mdash; autoplay tour</span></h1>
  <p>%(blurb)s</p>
  <div class="meta">%(meta)s</div>
</header>
<main>%(cards)s</main>
<script>
// The frames are cached for the life of the page: a clip asks for the same PNGs
// over and over, and the version keeps a page load from showing a stale tour.
const VER = '%(ver)s';
const cards = [...document.querySelectorAll('.card[data-frames]')];
cards.forEach((card, ci) => {
  const frames = card.dataset.frames.split(',');
  const img = card.querySelector('img');
  const badge = card.querySelector('.badge');
  const range = card.querySelector('input');
  const toggle = card.querySelector('button');
  let i = 0, playing = true;
  range.max = frames.length - 1;
  range.addEventListener('input', () => { playing = false; i = +range.value; show(); });
  toggle.addEventListener('click', () => { playing = !playing; toggle.textContent = playing ? 'Pause' : 'Play'; });
  function show() {
    img.src = '/shots/' + frames[i] + '?v=' + VER;
    badge.textContent = (i + 1) + ' / ' + frames.length;
    range.value = i;
  }
  show();
  setTimeout(() => setInterval(() => {
    if (!playing) return;
    i = (i + 1) %% frames.length;
    show();
  }, 90), ci * 35);
});
</script></body></html>
"""


def render(entries, ver):
    if not entries:
        return ("""<!doctype html><html><head><meta charset="utf-8">
<title>Muntcraft - no frames yet</title>
<style>body{background:#11151a;color:#dfe5ec;font:15px/1.6 system-ui,sans-serif}</style>
</head><body><div class="empty"><h1>No frames yet</h1>
<p>Run <code>sh tools/pcsim/play.sh</code> to build the harness, play the tour and
write the frames, then reload this page.</p></div></body></html>""").encode()

    cards = []
    for entry in entries:
        frames = entry["frames"]
        badge = "clip &middot; %d frames" % len(frames) if len(frames) > 1 else "still"
        bar = ""
        if len(frames) > 1:
            bar = ('<div class="bar"><button>Pause</button>'
                   '<input type="range" min="0" value="0">'
                   '<span style="font:11px ui-monospace,monospace;color:#78828f">frame</span></div>')
        cards.append(
            '<figure class="card" data-frames="%s">'
            '<div class="stage"><img alt="%s" src="/shots/%s">'
            '<span class="badge">%s</span></div>'
            '<figcaption><h2>%s</h2><p>%s</p>%s</figcaption></figure>'
            % (",".join(frames), entry["label"], frames[0], badge,
               entry["label"], entry["caption"] or "&mdash;", bar))

    total = sum(len(e["frames"]) for e in entries)
    clips = sum(1 for e in entries if len(e["frames"]) > 1)
    minutes, seconds = divmod(float(entries[-1]["time"]), 60)
    return (PAGE % {
        "cards": "\n".join(cards),
        "ver": ver,
        "blurb": ("The real game, driven headlessly by tools/pcsim: the same "
                  "sources as the desktop build, against a fake screen and a "
                  "virtual clock, with every frame written out as a PNG. "
                  "The clips play themselves."),
        "meta": ("%d shots, %d frames, %d clip%s &middot; %02d:%02d of game time &middot; "
                 "<a href=\"/manifest.txt\">manifest</a>"
                 % (len(entries), total, clips, "" if clips == 1 else "s",
                    int(minutes), int(seconds))),
    }).encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    shots = DEFAULT_SHOTS
    entries = []

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
        path = urllib.parse.urlparse(self.path).path

        if path in ("/", "/index.html"):
            self.entries = load_manifest(self.shots)  # a fresh tour shows up on reload
            try:
                ver = "%d" % os.path.getmtime(os.path.join(self.shots, "manifest.txt"))
            except OSError:
                ver = "0"
            self.respond(200, "text/html; charset=utf-8", render(self.entries, ver))
        elif path == "/manifest.txt":
            try:
                with open(os.path.join(self.shots, "manifest.txt"), "rb") as fh:
                    data = fh.read()
            except OSError:
                data = b""
            self.respond(200 if data else 404, "text/plain; charset=utf-8", data)
        elif path.startswith("/shots/"):
            # Only ever a bare file name from the shots directory.
            name = os.path.basename(urllib.parse.unquote(path[len("/shots/"):]))
            full = os.path.join(self.shots, name)
            try:
                with open(full, "rb") as fh:
                    data = fh.read()
            except OSError:
                data = b""
            self.respond(200 if data else 404, "image/png", data,
                         {"Cache-Control": "max-age=3600"})
        elif path == "/status":
            entries = load_manifest(self.shots)
            body = json.dumps({"dir": self.shots, "shots": len(entries),
                               "frames": sum(len(e["frames"]) for e in entries)}).encode()
            self.respond(200, "application/json", body)
        else:
            self.respond(404, "text/plain", b"not found")


def main():
    parser = argparse.ArgumentParser(description="Gallery of tour frames")
    parser.add_argument("--shots", default=os.environ.get("MUNT_SIM_SHOTS", DEFAULT_SHOTS))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", 8000)))
    args = parser.parse_args()

    Handler.shots = args.shots
    print("gallery of %s on http://0.0.0.0:%d/" % (args.shots, args.port), flush=True)
    ThreadingHTTPServer(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
