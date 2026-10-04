#!/usr/bin/env python3
"""Build a redistributable audio pack from permissively-licensed web sources.

Why this exists
---------------
The pack the game shipped with was assembled from three Minecraft asset archives
that carry no licence or credits file, which is why `crafti.audp` is gitignored
and why `AUDIO_OUTPUT_TEST.md` carries a licensing caveat. This script builds a
pack that can actually be shipped: every sample comes from a CC0 or CC-BY source
on OpenGameArt, and the mapping onto vanilla's event names is written out so it
can be audited.

What it does NOT do is download Minecraft's own sounds. They are proprietary, and
copying them off the web would leave exactly the problem this script exists to
fix. What you get instead is a pack that plays the right *cues* in the right
*places* with audio you are allowed to redistribute.

How the pieces fit
------------------
`build_audio_pack.py` numbers whatever files it is handed, deriving each id from
the path: `mob/pig/say1.ogg` becomes `MobPigSay1`. This script therefore does not
need to know the pack format at all -- it only has to put a file at the path each
vanilla event wants.

One constraint shapes the whole thing: `audio_sounds.h` is a plain enum and the
engine uses those names as compile-time constants, so **every name the engine
references must exist in the pack** or the calculator build fails to compile.
There is no per-sound fallback at that level. Events with no licensed sample get
a short generated placeholder, listed as SYNTHETIC in the manifest, so the pack
always builds and the game stays audible instead of going quiet.

Usage
-----
    python3 tools/audio/build_licensed_pack.py                 # fetch + build
    python3 tools/audio/build_licensed_pack.py --cache /tmp/x  # reuse a download
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OGA = "https://opengameart.org/sites/default/files/"

# --- the sources, and what each one is allowed to be used for --------------
# Everything below is downloaded straight from OpenGameArt's file store. The
# licence column is the one on the asset's own page; keep the two in step if a
# source is ever replaced.
SOURCES = {
    "sfx_100_v2.zip": {
        "url": OGA + "sfx_100_v2.zip",
        "page": "https://opengameart.org/content/100-cc0-sfx-2",
        "author": "rubberduck",
        "licence": "CC0 1.0",
        "dest": "sfx100",
    },
    "chicken_sound_effect.zip": {
        "url": OGA + "chicken_sound_effect.zip",
        "page": "https://opengameart.org/content/chicken-sound-effect",
        "author": "IMadeIt",
        "licence": "CC-BY 3.0",
        "dest": "chicken",
    },
    "loops.zip": {
        "url": OGA + "loops.zip",
        "page": "https://opengameart.org/content/pack-of-loopable-game-music",
        "author": "obscure music (Gichco)",
        "licence": "CC0 1.0",
        "dest": "loops",
    },
    "Alexander Ehlers - Free Music Pack.zip": {
        "url": OGA + "Alexander%20Ehlers%20-%20Free%20Music%20Pack.zip",
        "page": "https://opengameart.org/content/free-music-pack",
        "author": "Alexander Ehlers",
        "licence": "CC0 1.0",
        "dest": "ehlers",
    },
}

PIG = {  # Vinrax's Pig SFX Pack, CC-BY 3.0, uploaded as loose mp3s
    "url": OGA + "%s",
    "page": "https://opengameart.org/content/pig-sfx-pack",
    "author": "Vinrax",
    "licence": "CC-BY 3.0",
    "files": ["pig_idle.mp3", "pig_idle2.mp3", "pig_idle3.mp3",
              "pig_idle4.mp3", "pig_hit1.mp3", "pig_hit2.mp3"],
}

S = "sfx100/"
C = "chicken/"
L = "loops/"

# (path under sounds/, source key, why this source). "APPROX:" marks a judgement
# call -- a generic clip standing in for a material no licensed source covers.
MAP = [
    ("step/grass1.ogg", S + "sfx100v2_footstep_01.ogg", "generic footstep"),
    ("step/grass2.ogg", S + "sfx100v2_footstep_02.ogg", "generic footstep"),
    ("step/grass3.ogg", S + "sfx100v2_footstep_02.ogg", "generic footstep, reused"),
    ("step/wet_grass1.ogg", S + "sfx100v2_footstep_wet_01.ogg", "named wet footstep"),
    ("step/wet_grass2.ogg", S + "sfx100v2_footstep_wet_02.ogg", "named wet footstep"),
    ("step/wet_grass3.ogg", S + "sfx100v2_footstep_wet_03.ogg", "named wet footstep"),
    ("step/wood1.ogg", S + "sfx100v2_footstep_wood_01.ogg", "named wood footstep"),
    ("step/wood2.ogg", S + "sfx100v2_footstep_wood_02.ogg", "named wood footstep"),
    ("step/wood3.ogg", S + "sfx100v2_footstep_wood_03.ogg", "named wood footstep"),
    ("step/stone1.ogg", S + "sfx100v2_stones_01.ogg", "named stone step"),
    ("step/stone2.ogg", S + "sfx100v2_stones_02.ogg", "named stone step"),
    ("step/stone3.ogg", S + "sfx100v2_stones_03.ogg", "named stone step"),
    ("step/gravel1.ogg", S + "sfx100v2_metal_01.ogg", "APPROX: sharp granular impact"),
    ("step/gravel2.ogg", S + "sfx100v2_metal_02.ogg", "APPROX: sharp granular impact"),
    ("step/gravel3.ogg", S + "sfx100v2_metal_03.ogg", "APPROX: sharp granular impact"),
    ("step/sand1.ogg", S + "sfx100v2_metal_04.ogg", "APPROX: dull granular impact"),
    ("step/sand2.ogg", S + "sfx100v2_metal_05.ogg", "APPROX: dull granular impact"),
    ("step/sand3.ogg", S + "sfx100v2_metal_06.ogg", "APPROX: dull granular impact"),
    ("step/snow1.ogg", S + "sfx100v2_air_01.ogg", "APPROX: soft compression hiss"),
    ("step/snow2.ogg", S + "sfx100v2_air_02.ogg", "APPROX: soft compression hiss"),
    ("step/snow3.ogg", S + "sfx100v2_air_03.ogg", "APPROX: soft compression hiss"),

    ("dig/stone1.ogg", S + "sfx100v2_stones_01.ogg", "named stone"),
    ("dig/stone2.ogg", S + "sfx100v2_stones_02.ogg", "named stone"),
    ("dig/stone3.ogg", S + "sfx100v2_stones_03.ogg", "named stone"),
    ("dig/wood1.ogg", S + "sfx100v2_wood_hit_01.ogg", "named wood knock"),
    ("dig/wood2.ogg", S + "sfx100v2_wood_hit_02.ogg", "named wood knock"),
    ("dig/wood3.ogg", S + "sfx100v2_wood_hit_03.ogg", "named wood knock"),
    ("dig/grass1.ogg", S + "sfx100v2_footstep_01.ogg", "generic soft dig"),
    ("dig/grass2.ogg", S + "sfx100v2_footstep_02.ogg", "generic soft dig"),
    ("dig/grass3.ogg", S + "sfx100v2_wood_04.ogg", "generic soft dig"),
    ("dig/gravel1.ogg", S + "sfx100v2_metal_01.ogg", "APPROX: granular crush"),
    ("dig/gravel2.ogg", S + "sfx100v2_metal_02.ogg", "APPROX: granular crush"),
    ("dig/gravel3.ogg", S + "sfx100v2_metal_03.ogg", "APPROX: granular crush"),
    ("dig/sand1.ogg", S + "sfx100v2_metal_04.ogg", "APPROX: dull crush"),
    ("dig/sand2.ogg", S + "sfx100v2_metal_05.ogg", "APPROX: dull crush"),
    ("dig/sand3.ogg", S + "sfx100v2_metal_06.ogg", "APPROX: dull crush"),
    ("dig/snow1.ogg", S + "sfx100v2_air_01.ogg", "APPROX: soft crush"),
    ("dig/snow2.ogg", S + "sfx100v2_air_02.ogg", "APPROX: soft crush"),
    ("dig/snow3.ogg", S + "sfx100v2_air_03.ogg", "APPROX: soft crush"),

    ("random/dooropen.ogg", S + "sfx100v2_door_03.ogg", "longest door = the swing open"),
    ("random/doorclose.ogg", S + "sfx100v2_door_01.ogg", "short door = the latch shut"),
    ("random/chestopen.ogg", S + "sfx100v2_lock_open_01.ogg", "named lock/hinge"),
    ("random/chestclosed.ogg", S + "sfx100v2_lock_open_01.ogg", "named lock/hinge, reused"),
    ("random/click.ogg", S + "sfx100v2_switch_01.ogg", "named switch click"),
    ("random/break.ogg", S + "sfx100v2_glass_02.ogg", "named shatter"),
    ("random/pop.ogg", S + "sfx100v2_misc_07.ogg", "shortest clip = a pop"),
    ("random/levelup.ogg", S + "sfx100v2_misc_12.ogg", "APPROX: rising 2.5s cue"),
    ("random/hurt.ogg", S + "sfx100v2_hit_03.ogg", "short impact"),
    ("random/eat1.ogg", S + "sfx100v2_misc_11.ogg", "APPROX: short chewing"),
    ("random/eat2.ogg", S + "sfx100v2_misc_14.ogg", "APPROX: short chewing"),
    ("random/eat3.ogg", S + "sfx100v2_misc_26.ogg", "APPROX: short chewing"),
    ("random/fuse.ogg", S + "sfx100v2_air_02.ogg", "APPROX: 1.1s hiss, creeper-like"),
    ("random/explode1.ogg", S + "sfx100v2_misc_22.ogg", "APPROX: 3.2s blast"),
    ("random/explode2.ogg", S + "sfx100v2_misc_37.ogg", "APPROX: 4.9s blast"),
    ("random/explode3.ogg", S + "sfx100v2_misc_28.ogg", "APPROX: 2.0s blast"),

    ("ambient/cave/cave1.ogg", S + "sfx100v2_loop_ambient_02.ogg", "named ambient loop"),
    ("ambient/cave/cave2.ogg", S + "sfx100v2_loop_ambient_03.ogg", "named ambient loop"),
    ("ambient/cave/cave3.ogg", S + "sfx100v2_loop_machine_04.ogg", "APPROX: low machinery hum"),
    ("ambient/underwater/underwater_ambience.ogg", S + "sfx100v2_loop_water_01.ogg", "named water loop"),
    ("ambient/weather/rain1.ogg", S + "sfx100v2_loop_water_01.ogg", "APPROX: water hiss as rain"),
    ("ambient/weather/rain2.ogg", S + "sfx100v2_loop_water_02.ogg", "APPROX: water hiss as rain"),
    ("ambient/weather/rain3.ogg", S + "sfx100v2_loop_water_03.ogg", "APPROX: water hiss as rain"),
    ("ambient/weather/thunder1.ogg", S + "sfx100v2_thunder_01.ogg", "named thunder"),
    ("ambient/weather/thunder2.ogg", S + "sfx100v2_loop_construction_site.ogg", "APPROX: long low rumble"),
    ("ambient/weather/thunder3.ogg", S + "sfx100v2_loop_highway.ogg", "APPROX: long low rumble"),

    ("entity/player/damage/hit1.ogg", S + "sfx100v2_hit_01.ogg", "named hit"),
    ("entity/player/damage/hit2.ogg", S + "sfx100v2_hit_02.ogg", "named hit"),
    ("entity/player/damage/hit3.ogg", S + "sfx100v2_hit_03.ogg", "named hit"),
    ("damage/fallbig.ogg", S + "sfx100v2_metal_hit_01.ogg", "APPROX: heavy landing"),
    ("damage/fallsmall.ogg", S + "sfx100v2_hit_03.ogg", "APPROX: light landing"),
    ("entity/player/attack/strong1.ogg", S + "sfx100v2_hit_01.ogg", "named hit, heavier"),
    ("entity/player/attack/strong2.ogg", S + "sfx100v2_hit_02.ogg", "named hit, heavier"),
    ("entity/player/attack/strong3.ogg", S + "sfx100v2_hit_03.ogg", "named hit, heavier"),
    ("entity/player/attack/weak1.ogg", S + "sfx100v2_metal_hit_02.ogg", "APPROX: light swing"),
    ("entity/player/attack/weak2.ogg", S + "sfx100v2_metal_hit_02.ogg", "APPROX: light swing, reused"),
    ("entity/player/attack/weak3.ogg", S + "sfx100v2_metal_hit_02.ogg", "APPROX: light swing, reused"),
    ("item/armor/equip/generic1.ogg", S + "sfx100v2_items_01.ogg", "named item handling"),

    ("mob/pig/say1.ogg", "@pig_idle.mp3", "named pig idle"),
    ("mob/pig/say2.ogg", "@pig_idle2.mp3", "named pig idle"),
    ("mob/pig/say3.ogg", "@pig_idle3.mp3", "named pig idle"),
    ("mob/pig/death.ogg", "@pig_hit2.mp3", "named pig hit"),
    ("mob/chicken/say1.ogg", C + "Chicken Sound Effect.ogg", "named chicken call"),
    ("mob/creeper/say1.ogg", S + "sfx100v2_air_02.ogg", "APPROX: hiss, as a creeper makes"),
]

# music/menu1..4 are the title-screen pool; everything else is the in-game pool
# (audio_manager.cpp splits the two by those exact names).
MENU = [("Flags", ), ("Spacetime", ), ("Twists", ), ("Great mission", )]
GAME = [
    ("ehlers:Alexander Ehlers - Doomed.mp3", "Alexander Ehlers - Doomed"),
    ("ehlers:Alexander Ehlers - Waking the devil.mp3", "Alexander Ehlers - Waking the devil"),
    (L + "level1-step1.wav", "obscure music - level1-step1"),
    (L + "level1-step1-evil.wav", "obscure music - level1-step1-evil"),
    (L + "level1-step2.wav", "obscure music - level1-step2"),
    (L + "level1-step2-evil.wav", "obscure music - level1-step2-evil"),
    (L + "level1-step3.wav", "obscure music - level1-step3"),
    (L + "level1-step3-evil.wav", "obscure music - level1-step3-evil"),
]


def fetch(url, dst):
    if os.path.exists(dst) and os.path.getsize(dst) > 0:
        return
    print("  fetching %s" % os.path.basename(dst))
    with urllib.request.urlopen(url, timeout=300) as response:
        data = response.read()
    with open(dst, "wb") as out:
        out.write(data)


def download(cache, ex):
    os.makedirs(ex, exist_ok=True)
    for name, meta in SOURCES.items():
        path = os.path.join(cache, name)
        fetch(meta["url"], path)
        dest = os.path.join(ex, meta["dest"])
        if not os.path.isdir(dest):
            zipfile.ZipFile(path).extractall(dest)
    for f in PIG["files"]:
        fetch(PIG["url"] % f, os.path.join(cache, f))


def to_ogg(src, dst):
    """Container change only. The resample and dither chain stays the pack
    builder's job -- doing it here too would quantise twice."""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src,
                    "-c:a", "libvorbis", "-q:a", "5", dst],
                   check=True, timeout=300)


def placeholder(path, dst):
    """A short quiet decaying tone for an event with no licensed sample. The
    pitch is a stable hash of the path so two missing events are not the same
    beep."""
    h = int(hashlib.sha256(path.encode()).hexdigest(), 16)
    freq = 220 + (h % 500)
    decay = 6 + (h % 7)
    dur = 0.16 + ((h >> 8) % 5) * 0.03
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    "aevalsrc=0.22*sin(2*PI*t*%d)*exp(-%d*t):s=8000:d=%.3f"
                    % (freq, decay, dur),
                    "-c:a", "libvorbis", "-q:a", "4", dst],
                   check=True, timeout=120)


def engine_names():
    """The Sound:: names the engine actually references. Read out of the header
    the builder generates, so this never has to be kept in step by hand."""
    header = os.path.join(ROOT, "audio_sounds.h")
    names = {}
    with open(header) as handle:
        for line in handle:
            if "= " not in line or "//" not in line:
                continue
            ident = line.strip().split(" = ")[0]
            path = line.split("// ")[1].split(" (")[0]
            if ident and path:
                names[ident] = path
    return names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cache", default="/tmp/licaudio")
    ap.add_argument("--out", default=os.path.join(ROOT, "crafti.audp"))
    ap.add_argument("--header", default=os.path.join(ROOT, "audio_sounds.h"))
    ap.add_argument("--manifest", default=os.path.join(ROOT, "tools/audio/licensed_pack_mapping.tsv"))
    ap.add_argument("--music-seconds", type=float, default=120.0)
    args = ap.parse_args()

    cache, ex = args.cache, os.path.join(args.cache, "ex")
    stage = os.path.join(args.cache, "stage")
    sounds = os.path.join(stage, "sounds")
    music = os.path.join(stage, "music")

    print("sources:")
    download(cache, ex)
    for d in (sounds, music):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)

    rows = []

    def resolve(key):
        if key.startswith("@"):
            return os.path.join(cache, key[1:])
        if key.startswith("ehlers:"):
            import glob
            hits = glob.glob(os.path.join(ex, "ehlers", "*", key[7:]))
            if not hits:
                sys.exit("missing music source: %s" % key)
            return hits[0]
        return os.path.join(ex, key)

    print("staging licensed samples:")
    for rel, key, why in MAP:
        to_ogg(resolve(key), os.path.join(sounds, rel))
        rows.append((rel, key, why))

    for i, (base,) in enumerate(MENU, 1):
        to_ogg(resolve("ehlers:Alexander Ehlers - %s.mp3" % base),
               os.path.join(music, "menu%d.ogg" % i))
        rows.append(("music/menu%d.ogg" % i, "Ehlers - %s" % base, "title pool"))

    for i, (key, label) in enumerate(GAME, 1):
        to_ogg(resolve(key), os.path.join(music, "track%d.ogg" % i))
        rows.append(("music/track%d.ogg" % i, label, "in-game pool"))

    # Every name the engine references has to exist or the CX build will not
    # compile; fill the gaps rather than dropping them.
    print("filling gaps with generated placeholders:")
    name_to_path = engine_names()
    # The pack builder is handed --sounds and --music as *separate* roots, so a
    # pack path is relative to whichever one it came from. Comparing against the
    # stage root instead would put a "sounds/" prefix on every path and quietly
    # match nothing, turning the whole pack into placeholders.
    staged = set()
    for base in (sounds, music):
        for root, _, files in os.walk(base):
            for f in files:
                rel = os.path.relpath(os.path.join(root, f), base)
                staged.add(rel.replace(os.sep, "/"))

    referenced = set()
    for root, _, files in os.walk(ROOT):
        if any(part in (".git", "build", "tests") for part in root.split(os.sep)):
            continue
        for f in files:
            if f.endswith((".cpp", ".h")):
                with open(os.path.join(root, f), errors="ignore") as handle:
                    for line in handle:
                        i = line.find("Sound::")
                        while i >= 0:
                            tail = line[i + 7:]
                            ident = ""
                            for ch in tail:
                                if ch.isalnum() or ch == "_":
                                    ident += ch
                                else:
                                    break
                            if ident in name_to_path:
                                referenced.add(name_to_path[ident])
                            i = line.find("Sound::", i + 7)

    synthetic = []
    for path in sorted(referenced):
        if path in staged:
            continue
        placeholder(path, os.path.join(sounds, path))
        synthetic.append(path)
        rows.append((path, "(generated placeholder)",
                     "SYNTHETIC: no licensed source for %s" % path))

    with open(args.manifest, "w") as out:
        out.write("pack path\tsource\twhy\n")
        for a, b, c in rows:
            out.write("%s\t%s\t%s\n" % (a, b, c))

    print("building the pack:")
    subprocess.run([sys.executable, os.path.join(HERE, "build_audio_pack.py"),
                    "--sounds", sounds, "--music", music,
                    "--out", args.out, "--header", args.header,
                    "--music-seconds", str(args.music_seconds)], check=True)

    with open(os.path.join(ROOT, "AUDIO_LICENSES.md"), "w") as out:
        out.write("# Audio pack licences\n\n")
        out.write("Generated by `tools/audio/build_licensed_pack.py`. Every sample in\n")
        out.write("`crafti.audp` comes from one of the sources below. The Minecraft\n")
        out.write("assets themselves are **not** used: they carry no licence, which is why\n")
        out.write("this pack exists.\n\n")
        out.write("## Sources\n\n")
        out.write("| source | author | licence | page |\n|---|---|---|---|\n")
        for name, meta in sorted(SOURCES.items()):
            out.write("| `%s` | %s | %s | %s |\n"
                      % (name, meta["author"], meta["licence"], meta["page"]))
        out.write("| `pig_*.mp3` | %s | %s | %s |\n"
                  % (PIG["author"], PIG["licence"], PIG["page"]))
        out.write("\nCC0 needs no credit; the CC-BY ones are credited above as their\n")
        out.write("licences require.\n\n")
        out.write("## Coverage\n\n")
        out.write("%d licensed clips, %d generated placeholders, %d events the engine\n"
                  % (len(rows) - len(synthetic), len(synthetic), len(referenced)))
        out.write("references. Events with no licensed sample fall back to a short\n")
        out.write("generated tone so the pack always builds; each one is marked\n")
        out.write("`SYNTHETIC` in `tools/audio/licensed_pack_mapping.tsv`.\n\n")
        out.write("The pack is **redistributable**. `crafti.audp` is still gitignored only\n")
        out.write("because it is a 6 MB build product -- nothing about its contents is\n")
        out.write("encumbered any more.\n")

    print("manifest: %s" % args.manifest)


if __name__ == "__main__":
    main()