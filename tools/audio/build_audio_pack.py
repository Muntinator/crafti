#!/usr/bin/env python3
"""Build the Crafti on-calculator audio pack from the provided asset archives.

Input   : extracted OGG sound effects (sounds_trimmed.zip -> extracted/) and
          MP3 music tracks (minecraft-essentials-music.zip -> music/).
Output  : a single streamable pack file (default `crafti.audp`) plus a generated
          C++ header (`audio_sounds.h`) that numbers the sound ids in exactly the
          same order as the pack index, so the engine needs no runtime lookup.

On-device format
----------------
The dock output is a single digital pin, so everything below is optimised for a 1-bit
output stage driven by a sigma-delta modulator.  The pack therefore stores
8-bit unsigned mono PCM; the mixer converts that to 1-bit inside the output
backend.  8-bit unsigned keeps the on-calculator decode trivial (a byte is a
sample) and keeps the pack small enough to stream from flash.

Because the output is 1-bit, the two things that decide how good the pack can
sound are the resampler that takes the sources down to the mixer rate and the
quantiser that lands them on 8 bits:

  * the sources are resampled with **soxr** and its anti-aliasing filter.  The
    default swr resampler is soft, and a soft downsample folds high-frequency
    content back into the audible band; the official samples are 44.1 kHz, so
    this is the single biggest difference in how the pack sounds,
  * a gentle **high-pass at 15 Hz** removes any DC the source carries.  A DC
    offset eats the 1-bit modulator's headroom and turns into an idle tone,
  * the 8-bit quantiser is **TPDF dithered** (triangular, noise-shaped), which
    replaces the correlated quantisation distortion of truncation with a hiss
    that is 15 dB lower in the audible band,
  * music is kept whole (up to `--music-seconds`) instead of being cut after 75
    seconds, and gets a short fade at both ends so the loop point does not click.

Usage
-----
    python3 tools/audio/build_audio_pack.py \
        --sounds /tmp/audiosrc/extracted \
        --music  /tmp/audiosrc/music \
        --out    crafti.audp \
        --header audio_sounds.h
"""

import argparse
import math
import os
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# Audio the far end reconstructs: how far above the mixer rate the anti-alias
# filter is allowed to pass, how steep soxr's filter is, and how hard the
# quantiser is dithered.  See the module docstring for why each one is here.
RESAMPLER = "soxr"
RESAMPLER_PRECISION = 28
RESAMPLER_CUTOFF = 0.95
DITHER_METHOD = "triangular_hp"
DC_HIGHPASS_HZ = 15
# Short fade at each end of a sound, in seconds.  The engine loops a bed by
# restarting the file, so a track that starts or ends mid-waveform clicks at every
# wrap; 20 ms is inaudible as a fade but removes the step.  Effects get a much
# shorter one because they are capped by --sfx-seconds: a sound longer than the
# cap is cut mid-waveform, and a few milliseconds of fade turns that cut from a
# click into an ending.
MUSIC_EDGE_FADE_SECONDS = 0.02
SFX_EDGE_FADE_SECONDS = 0.004

PACK_MAGIC = b"AUD1"
PACK_VERSION = 1

# Keep this list in sync with GameAudio::Category in audio_manager.h.
CATEGORY_NAMES = [
    "UI",
    "Music",
    "Ambience",
    "Weather",
    "Blocks",
    "Footsteps",
    "Mobs",
    "Player",
    "Combat",
]
CAT = {name: index for index, name in enumerate(CATEGORY_NAMES)}

FLAG_LOOP = 1 << 0
FLAG_MUSIC = 1 << 1
FLAG_STREAM = 1 << 2

# Pack entry layout, kept in sync with GameAudioPack::Entry in audio_pack.h.
ENTRY_SIZE = 16


def category_for(rel_path):
    """Map a source path to a mixer category.

    The categories are the sliders the settings screen exposes, so they have to
    follow vanilla's split rather than the folders: the local player's own hurts
    and eating belong to Player, their attacks to Combat, and a UI click must not
    be attenuated like a mob. Anything entity shaped that is not the player is a
    mob, and the misc `random/` pool is sorted by what the sound is for.
    """
    parts = rel_path.split("/")
    top = parts[0]
    base = parts[-1].lower()
    second = parts[1] if len(parts) > 1 else ""

    if top == "step":
        return CAT["Footsteps"]
    if top in ("dig", "block", "tile", "note", "portal", "fire", "liquid"):
        return CAT["Blocks"]
    if top == "ambient":
        if second == "weather":
            return CAT["Weather"]
        return CAT["Ambience"]
    if top == "music":
        return CAT["Music"]
    if top in ("mob", "entity"):
        # `entity/player` is the local player, not a mob: a hurt or an eat is the
        # player slider, an attack swing is combat.
        if top == "entity" and second == "player":
            if len(parts) > 2 and parts[2] == "attack":
                return CAT["Combat"]
            return CAT["Player"]
        return CAT["Mobs"]
    if top == "damage":
        return CAT["Combat"]
    if top == "item":
        return CAT["Player"]
    if top == "random":
        if base.startswith(("explode", "fuse", "glass", "break", "fizz", "ignite")):
            return CAT["Blocks"]
        if base.startswith(("bowhit", "classic_hurt", "successful_hit")):
            return CAT["Combat"]
        if base.startswith(("eat", "drink", "burp", "pop", "orb", "levelup", "breath")):
            return CAT["Player"]
        return CAT["UI"]
    return CAT["UI"]


def flags_for(rel_path):
    parts = rel_path.split("/")
    top = parts[0]
    flags = 0
    if top == "music":
        return FLAG_MUSIC | FLAG_STREAM | FLAG_LOOP
    if top == "ambient":
        flags |= FLAG_LOOP | FLAG_STREAM
        if len(parts) > 1 and parts[1] == "weather":
            pass
        return flags
    if top == "liquid":
        # Water/lava beds loop; the short splash/bubble variants do not.
        base = parts[-1].lower()
        if base.startswith(("water", "lava")):
            flags |= FLAG_LOOP
    return flags


def ident_for(rel_path):
    """step/grass1.ogg -> StepGrass1 ; music/11 - Mice on Venus.mp3 -> Music11MiceOnVenus"""
    path = rel_path.rsplit(".", 1)[0]
    ident = "".join(ch if ch.isalnum() else " " for ch in path)
    words = [w for w in ident.split() if w]
    name = "".join(w[0].upper() + w[1:] for w in words)
    if not name or name[0].isdigit():
        name = "S" + name
    return name


def decode_filters(rate):
    """The ffmpeg filter chain one source file is put through.

    Order matters: the channels are collapsed to mono first so the resampler
    runs once, the DC block then runs at the source rate, and soxr does the one
    and only rate conversion and the dithered landing on 8-bit unsigned.
    """
    return ",".join([
        "aformat=channel_layouts=mono",
        "highpass=f=%d" % DC_HIGHPASS_HZ,
        "aresample=resampler=%s:precision=%d:cutoff=%.2f:osf=u8:osr=%d:dither_method=%s"
        % (RESAMPLER, RESAMPLER_PRECISION, RESAMPLER_CUTOFF, rate, DITHER_METHOD),
    ])


def ffmpeg_decode(path, rate, max_seconds):
    """Return (pcm_bytes, truncated) for one source file, as 8-bit unsigned mono."""
    cmd = [
        "ffmpeg",
        "-v", "error",
        "-i", path,
    ]
    if max_seconds and max_seconds > 0:
        cmd += ["-t", "%.3f" % max_seconds]
    cmd += [
        "-af", decode_filters(rate),
        "-f", "u8",
        "-",
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if proc.returncode != 0 or not proc.stdout:
        raise RuntimeError(proc.stderr.decode("utf-8", "replace").strip() or "ffmpeg failed")
    truncated = bool(max_seconds and max_seconds > 0
                     and len(proc.stdout) >= int(rate * max_seconds) - 1)
    return proc.stdout, truncated


def edge_fade(pcm, rate, seconds):
    """Fade a sound in and out so it cannot click at either end.

    The engine restarts the file to loop a bed, so unless the first and last
    samples both sit at silence the wrap is heard as a tick; and a sound cut off
    by the duration cap ends mid-waveform, which is a click of its own.
    """
    frames = int(rate * seconds)
    if frames <= 0 or frames * 2 >= len(pcm):
        return pcm

    out = bytearray(pcm)
    for i in range(frames):
        # Equal-power-ish: a raised-cosine reaches 1.0 with no derivative kink.
        gain = 0.5 - 0.5 * math.cos(math.pi * i / frames)
        head = 128 + (pcm[i] - 128) * gain
        tail = 128 + (pcm[len(pcm) - 1 - i] - 128) * gain
        out[i] = min(255, max(0, int(head + 0.5)))
        out[len(pcm) - 1 - i] = min(255, max(0, int(tail + 0.5)))
    return bytes(out)


def normalise(pcm):
    """Scale 8-bit unsigned mono so its peak is close to full scale.

    Keeps voice loudness predictable in the mixer without storing per-sound
    gains.  Near-silent material is left untouched so hiss is not amplified.
    The scale rounds to nearest rather than truncating, which would add a
    systematic half-bit downward bias to every sample of every sound.
    """
    peak = 0
    for sample in pcm:
        deviation = sample - 128 if sample >= 128 else 128 - sample
        if deviation > peak:
            peak = deviation
    if peak < 8 or peak >= 120:
        return pcm, 255
    gain = min(255, int(120 * 255 / peak))

    def scale(sample):
        scaled = (sample - 128) * gain / 255.0
        rounded = int(scaled + 0.5) if scaled >= 0 else -int(-scaled + 0.5)
        return min(255, max(0, 128 + rounded))

    return bytes(scale(sample) for sample in pcm), gain


def collect(root, pattern_exts):
    found = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if os.path.splitext(name)[1].lower() in pattern_exts:
                full = os.path.join(dirpath, name)
                found.append((os.path.relpath(full, root).replace(os.sep, "/"), full))
    return sorted(found)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sounds", help="directory holding extracted/*.ogg")
    ap.add_argument("--music", help="directory holding *.mp3")
    ap.add_argument("--out", default="crafti.audp")
    ap.add_argument("--header", default="audio_sounds.h")
    ap.add_argument("--rate", type=int, default=8000)
    ap.add_argument("--sfx-seconds", type=float, default=1.1)
    ap.add_argument("--music-seconds", type=float, default=240.0,
                    help="keep this much of each music track (0 = no limit); "
                         "the engine loops a track, so a short cut is heard as a jump")
    ap.add_argument("--jobs", type=int, default=0,
                    help="parallel ffmpeg decodes (0 = one per CPU, capped at 8)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    jobs = args.jobs if args.jobs > 0 else min(8, (os.cpu_count() or 1))

    entries = []
    if args.sounds and os.path.isdir(args.sounds):
        entries += [(rel, full, False) for rel, full in collect(args.sounds, {".ogg", ".wav"})]
    if args.music and os.path.isdir(args.music):
        entries += [(rel, full, True) for rel, full in collect(args.music, {".mp3", ".ogg", ".wav"})]

    if not entries:
        print("no input files found; nothing to do", file=sys.stderr)
        return 1

    # Deterministic id order: music last (so music ids stay stable if SFX change
    # is not a goal either -- everything is regenerated together).
    entries.sort(key=lambda e: (e[2], e[0]))

    index = []
    blobs = []
    offset = 0
    used_names = {}
    failures = []

    def decode(entry):
        rel, full, is_music = entry
        rel = ("music/" + rel) if (is_music and not rel.startswith("music/")) else rel
        if is_music:
            rel = rel.replace("music/music/", "music/")
        max_seconds = args.music_seconds if is_music else args.sfx_seconds
        try:
            pcm, truncated = ffmpeg_decode(full, args.rate, max_seconds)
            pcm = edge_fade(pcm, args.rate,
                            MUSIC_EDGE_FADE_SECONDS if is_music else SFX_EDGE_FADE_SECONDS)
        except Exception as exc:  # keep going, report at the end
            return (rel, None, str(exc))
        return (rel, pcm, None)

    # Decoding is one ffmpeg process per source file. There are over a thousand
    # of them, so they run in a pool; map() keeps the results in id order, which
    # is what makes the pack's index (and therefore audio_sounds.h) deterministic.
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        decoded = list(pool.map(decode, entries))

    for rel, pcm, error in decoded:
        if pcm is None:
            failures.append((rel, error))
            continue

        name = ident_for(rel)
        if name in used_names:
            used_names[name] += 1
            name = "%s%d" % (name, used_names[name])
        else:
            used_names[name] = 1

        pcm, gain = normalise(pcm)
        index.append((offset, len(pcm), name, category_for(rel), flags_for(rel), gain, rel))
        blobs.append(pcm)
        offset += len(pcm)

    if not index:
        print("every input file failed to decode", file=sys.stderr)
        return 1

    # 24 byte header: magic, version, rate, flags, count, index, data, size.
    index_offset = 24
    data_offset = index_offset + ENTRY_SIZE * len(index)

    with open(args.out, "wb") as out:
        out.write(struct.pack(
            "<4sHHHHIII",
            PACK_MAGIC, PACK_VERSION, args.rate, 0, len(index), index_offset, data_offset,
            offset))
        for off, length, _name, cat, flags, gain, _rel in index:
            out.write(struct.pack("<IIHHHH", off, length, cat, flags, gain, 0))
        for blob in blobs:
            out.write(blob)

    with open(args.header, "w") as header:
        header.write(
            "// Generated by tools/audio/build_audio_pack.py -- do not edit by hand.\n"
            "// Ids are numbered in exactly the order of the pack index table.\n"
            "#ifndef AUDIO_SOUNDS_H\n#define AUDIO_SOUNDS_H\n\n"
            "namespace GameAudio\n{\nnamespace Sound\n{\nenum Id\n{\n"
            "    None = 0,\n")
        for number, (_off, _len, name, cat, flags, _gain, rel) in enumerate(index, start=1):
            header.write("    %s = %d, // %s (%s%s)\n" % (
                name, number, rel, CATEGORY_NAMES[cat],
                ", loop" if flags & FLAG_LOOP else "",
            ))
        header.write(
            "    Count = %d\n};\n}\n}\n\n#endif // AUDIO_SOUNDS_H\n" % len(index))

    total = data_offset + offset
    if not args.quiet:
        print("pack      : %s (%d sounds, %d bytes, %d KiB)" % (
            args.out, len(index), total, (total + 1023) // 1024))
        print("header    : %s" % args.header)
        print("rate      : %d Hz, 8-bit unsigned mono" % args.rate)
        by_cat = {}
        for _off, length, _name, cat, _flags, _gain, _rel in index:
            count, size = by_cat.get(cat, (0, 0))
            by_cat[cat] = (count + 1, size + length)
        for cat, (count, size) in sorted(by_cat.items()):
            print("  %-10s %4d sounds %7d bytes" % (CATEGORY_NAMES[cat], count, size))
        if failures:
            print("failed    : %d file(s)" % len(failures), file=sys.stderr)
            for rel, message in failures[:10]:
                print("  %s: %s" % (rel, message), file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
