# Running Muntcraft in an emulator

`tools/emu/` builds a **headless** TI-Nspire CX emulator and drives it from a
script: no GUI, no X server, no calculator. It exists so the CX-only parts of
Muntcraft — the title screen, the cached dirt backdrop, the stone wordmark, the
pause menu, bed halves, snow layers, the night sky — can be looked at as real
320x240 screenshots instead of only being asserted by host tests.

The emulation core is [Firebird](https://github.com/nspire-emus/firebird)
(GPLv3), the community TI-Nspire emulator. `munt-headless.cpp` is a small
GPLv3-licensed frontend for it, so this tool stays out of the game's build:
`crafti.tns` is unaffected and nothing here ships to the calculator.

## Setup

```sh
sh tools/emu/setup_firebird.sh     # clones + builds into $HOME/firebird
sh tools/emu/selftest.sh           # proves the setup works (see below)
```

`setup_firebird.sh` needs `git`, `make`, `g++` and `libz`. It clones Firebird
into `$FIREBIRD_DIR` (default `$HOME/firebird`), builds the emulation core, and
links `$HOME/firebird/munt-headless` against it. Override with
`FIREBIRD_DIR=... FIREBIRD_REF=... sh tools/emu/setup_firebird.sh`.

`selftest.sh` runs a tiny bare-metal ARM payload that paints the LCD and then
checks the captured PNG pixel by pixel. It needs no calculator dump and no OS
image, so it works on a clean machine:

```
== checking the captured pixels
  ok   inside the top band -> 255 255 255
  ...
PASS: emulator + screenshots verified (/tmp/munt-emu-selftest/shots/final.png)
```

## What you have to supply

The emulator needs copyrighted files that cannot be shipped with this repo:

1. **`boot1.img`** — a 512 KiB dump of the CX boot ROM. Nothing runs without it
   and there is no free substitute.
2. **An OS image** — either a ready-made flash image, or the TI OS file for the
   CX (e.g. `TI-NspireCX-4.5.0.tcc` / `.tcc`), which `--create-flash` flashes
   into a fresh image so a flash dump is not strictly needed.
3. **Ndless** for that exact OS version — `ndless.me` lists the supported
   versions (note the gap: 4.3.0 is *not* supported). The installers and
   `ndless_resources.tns` come from the
   [Ndless releases](https://github.com/ndless-nspire/Ndless/releases).

Use a CX/CX CAS revision with Ndless support (4.2.0, 4.4.0, 4.5.x). Put the
files somewhere outside the repo, e.g. `~/nspire-kit/`; they are large and
must not be committed.

### Getting the files without a calculator

The boot ROM is dumpable from real hardware only. If you do not have it, a
commonly used route is a community download page that ships a `boot1.img`, a
flash image and the OS; the CX OS itself is also a free download from TI. The
archive.org item `ti-nspire-os-fire-bird-ndless-v-4.5.4.48` bundles a CX CAS
4.5.4.48 OS, `boot1`/`boot2`, a prepared flash image and the matching Ndless
files, which is what this workspace used:

```sh
mkdir -p ~/nspire-kit && cd ~/nspire-kit
curl -L -o kit.zip 'https://archive.org/download/ti-nspire-os-fire-bird-ndless-v-4.5.4.48/TI-nspire%20OS%20%2BFireBird%2BNdless%20%28v4.5.4.48%29.zip'
unzip -j kit.zip '*/flash/flash' '*/boot1+boot2/*' '*/OS/*' '*/ndless/*'
```## Creating a flash image

```sh
EMU=$HOME/firebird/munt-headless
$EMU --model cx --create-flash cx.img --os TI-NspireCX-4.5.0.tcc
$EMU --info cx.img            # -> "cx.img: CX (HW A)"
```

`--model` accepts `cx` (default), `cxcas`, `cx2`, `touchpad`, `touchpadcas`.
The image is 132 MB and boots the real OS in the emulator. Alternatively take
a prepared `flash` image from a kit and its matching `boot1`.

Watch out: **flash writes are buffered in memory** and only reach the file when
the run ends through `--run`. That is what persists a transferred file or an
installed OS, so always let `--run` expire instead of killing the process.

## Copying files onto the calculator

Files are pushed over the emulated USB link, which the guest OS can only
enumerate once it has booted, so the transfers are scheduled with `--put-at`:

```sh
$EMU --boot1 boot1.img --flash cx.img \
     --put-at 25 --mkdir /ndless/startup \
     --put crafti.tns:/ndless/startup/crafti.tns \
     --run 70
```

* Paths are relative to the OS's documents folder, which the OS prefixes with
  `/documents` itself: ask for `/ndless/startup/crafti.tns`, **not**
  `/documents/ndless/startup/crafti.tns` (that creates a nonexistent path and
  the transfer fails with `utf8rename: errno=2`).
* `--mkdir` is needed for a folder that does not exist yet: `PutFile` does not
  create it, and the rename at the end of the transfer fails without it.
* The run prints `usblink connected`, then `transfer complete` per file.
* Do the transfer in one run and boot again in the next: by the time the link
  comes up, Ndless has already scanned its startup folder.

## Running Muntcraft

Ndless executes everything in `/ndless/startup` at boot, which is how Muntcraft
starts without touching the keypad:

```sh
$EMU --boot1 boot1.img --flash cx.img --put-at 25 \
     --put crafti.tns:/ndless/startup/crafti.tns --run 70 \
     --shot-dir shots --shot-at 5,15,25,35,45,55,65
```

Screenshots land in `shots/` as `shot-0010.0s.png` plus a `final.png`.

### Installing Ndless

A flash image that merely *contains* `ndless_installer_*.tns` and
`ndless_resources.tns` is not enough - Ndless has to be installed once, which
patches the OS in flash. Until that has happened nothing in `/ndless/startup`
runs (this was verified with a stock Ndless sample, which also stayed dormant).

To install it, drive the OS file browser with `--keys` and watch the captured
screenshots: the installer lives in the `ndless` folder, and running it patches
the OS and reboots. Key names available: `esc`, `tab`, `menu`, `doc`, `enter`,
`del`, `cat`, `var`, the touchpad as `up`/`down`/`left`/`right`/`click`, plus
letters and digits for typing.

## Driving it

| Option | Meaning |
| --- | --- |
| `--boot1`, `--flash` | The two images (required to boot) |
| `--create-flash`, `--model`, `--os` | Write a new flash image and exit |
| `--info <file>` | Print a flash image's model |
| `--install-os <file>` | Send an OS to a calculator showing the install prompt |
| `--put <local>:<remote>` | Copy a file onto the calculator (repeatable) |
| `--mkdir <remote>`, `--put-at <s>` | Create a folder first / when to start transfers |
| `--dump-lcd` | Dump the rendered panel (raw RGB565) |
| `--shot-dir`, `--shot-at 3,10,30` | Where and when to capture (virtual seconds) |
| `--shot-every`, `--shot-format png\|ppm` | Fixed interval / format |
| `--keys 12.5:esc,13:enter` | Key script, in virtual seconds |
| `--type 20:give 5` | Type text (letters, digits, space, dot) |
| `--run <seconds>` / `--keep-running` | How much virtual time to emulate |
| `--realtime` | Throttle to real speed instead of running flat out |
| `--dump-lcd <file>`, `--dump-mem <a:len:file>` | Debug dumps of the panel / memory |

Key names: `esc`, `tab`, `menu`, `doc`, `enter`, `ret`, `space`, `del`, `ctrl`,
`shift`, `on`, `pad`, `cat`, `var`, `flag`, `dot`, `minus`, `plus`, `a`-`z`,
`0`-`9`, plus the touchpad as `up`, `down`, `left`, `right` and `click`. Prefix
a name with `-` to release it without pressing it.

Time is **virtual**: the emulator's 100Hz scheduler drives the scripts, so
screenshots and key presses land identically whether it runs at 1x or 50x.
A CX boots its OS in a couple of virtual seconds; give it 30-60.

## Notes and limits

* The panel is rendered by Firebird's own routine — the same code the Qt GUI
  uses — so these screenshots show exactly what the emulator displays. Black and
  white are the only format-independent values in a raw framebuffer: a CX reads
  framebuffer pixels as BGR565 unless LCD control bit 8 selects RGB, so a guest
  writing colours has to match what the OS programs. `selftest/lcd_payload.s`
  therefore only paints black and white.
* A capture is a *panel* capture: it shows what the emulated LCD holds, not the
  backlight state. The emulator renders a CX CAS 4.5.4.48 home screen as a
  mostly black field with white text and blue accents, so judge output by shape,
  colour and text rather than by overall brightness. Reading text out of a
  320x240 capture works: upscale it (`ffmpeg -i shot.png -vf scale=1280:960`) and
  run `tesseract` over the result.
* **Audio is not emulated.** `audio_tx_hw.h` drives the dock UART directly,
  which only exists on real hardware; sound can only be checked on a calculator.
* The emulator cannot tell you about speed. Use `--realtime` to see whether the
  OS keeps up, but CX timings still need hardware.
* File transfers are exercised here (that is how `crafti.tns` gets onto the
  calculator); `--install-os` and a full OS install have not been.
