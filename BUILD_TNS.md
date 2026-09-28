Build .tns (Ndless)
===================

Quick steps
-----------

1. Open a terminal in this folder (the repository root).
2. Build:

   make -j4

3. Output file:

   crafti.tns

   (`make` runs `nspire-ld` -> `genzehn` -> `make-prg`; `crafti.tns` is always
   normalized to that name, even if `make-prg` produces a versioned filename.)

`NDLSSDK=...` and `ZLIB_PREFIX=...` are also accepted and add the Ndless/zlib
`-I` and `-L` paths explicitly:

   make -j4 NDLSSDK=/opt/ndless-sdk ZLIB_PREFIX=/opt/arm-zlib

They are optional when the `nspire-gcc` / `nspire-g++` / `nspire-ld` wrappers
already add those paths (see below).

Requirements
------------

On `PATH`:

- `nspire-gcc`, `nspire-g++`  (compile)
- `nspire-ld`                 (link)
- `genzehn`, `make-prg`       (packaging, shipped in the Ndless SDK `bin/`)

---

Verified Linux (Debian/Ubuntu) setup
-----------------------------------

This is the environment in which `make` was confirmed to produce a working
`crafti.tns`. Paths are examples - any prefix works.

### 1. Host cross toolchain + C/C++ libraries

    sudo apt-get install -y \
        gcc-arm-none-eabi g++-arm-none-eabi \
        binutils-arm-none-eabi \
        libnewlib-arm-none-eabi libnewlib-dev \
        libstdc++-arm-none-eabi-dev libstdc++-arm-none-eabi-newlib \
        build-essential texinfo bison flex git

(`gcc-arm-none-eabi` 10.3.1 provides `libc.a`/`libm.a` in
`/usr/lib/arm-none-eabi/lib`, `libstdc++.a` in the same directory and
`libgcc.a` in `/usr/lib/gcc/arm-none-eabi/10.3.1`.)

### 2. Ndless SDK

    git clone --recursive https://github.com/ndless-nspire/Ndless.git /tmp/ndless
    # SDK tree lives in /tmp/ndless/ndless-sdk

Then build the SDK libraries, crt objects and tools (`libsyscalls`,
`libndls`, `thirdparty/*` including `nspire-io`, `tools` including `genzehn`,
and `system` which produces `crt0.o`/`crti.o`/`crtn.o`):

    make -C /tmp/ndless/ndless-sdk -j2 \
        build-libsyscalls build-libndls build-thirdparty build-tools build-system

Note: building `libsyscalls` needs `PATH_MAX`, which Debian's newlib does not
define; add `-DPATH_MAX=4096` to that one build (SDK-side workaround only, no
change to this repository).

### 3. ARM zlib (used by the save system)

    git clone https://github.com/madler/zlib /tmp/zlib-build
    cd /tmp/zlib-build
    CC=arm-none-eabi-gcc CFLAGS="-O2 -marm -mcpu=arm926ej-s" \
        ./configure --static --prefix=/tmp/arm-zlib
    make -j2 && make install

### 4. linker

The Debian `arm-none-eabi-ld` (2.38) cannot parse the Ndless linker script
(`SORT_BY_INIT_PRIORITY(REVERSE(...))` -> "syntax error"). Build a newer
binutils and install just `ld`:

    ./configure --target=arm-none-eabi --disable-nls --disable-werror \
        --enable-targets=arm-none-eabi --prefix=/tmp/arm-tools
    make -j2 all-ld && make -C ld install-ld

### 5. Wrapper scripts (put these in `/usr/local/bin`)

`nspire-gcc` / `nspire-g++` - same body, `arm-none-eabi-gcc` or `g++`:

    #!/bin/sh
    exec arm-none-eabi-gcc -mcpu=arm926ej-s -D_TINSPIRE "$@" \
        -I /tmp/ndless/ndless-sdk/include \
        -I /tmp/ndless/ndless-sdk/include/freetype2 \
        -I /tmp/arm-zlib/include

`nspire-ld`:

    #!/bin/sh
    NDLESS=/tmp/ndless/ndless-sdk
    exec arm-none-eabi-g++ -mcpu=arm926ej-s -nostdlib -nostartfiles -static \
        -B/tmp/arm-tools/bin \
        -Wl,--pic-veneer -Wl,--emit-relocs "-Wl,-T,$NDLESS/system/ldscript" \
        "$NDLESS/system/crt0.o" "$NDLESS/system/crti.o" /tmp/ndless-shim/crtaux.o \
        "$@" \
        -L"$NDLESS/lib" -L/tmp/arm-zlib/lib \
        -L/usr/lib/arm-none-eabi/lib -L/usr/lib/gcc/arm-none-eabi/10.3.1 \
        -Wl,--start-group -lnspireio -lndls -lsyscalls -lstdc++ -lz -lm -lgcc -lc \
        -Wl,--end-group "$NDLESS/system/crtn.o"

`genzehn`, `make-prg`, `nspire-as` and `nspire-tools` come from the SDK, so
symlink them into the same `bin` directory:

    for t in genzehn make-prg nspire-as nspire-tools; do \
        ln -sf /tmp/ndless/ndless-sdk/bin/$t /usr/local/bin/$t; done

### 6. `_init` / `_fini` shim

The SDK ships its own `crt0.o`/`crti.o`/`crtn.o`; its `crtn.o` deliberately
omits `_init`/`_fini` because Ndless runs C++ ctors/dtors through
`__cpp_init`/`__cpp_fini`. Debian's newlib `libc.a` still pulls in
`__libc_fini_array` (via `__call_atexit.o`), which needs both symbols, so link a
tiny no-op stub:

    mkdir -p /tmp/ndless-shim
    printf '\t.text\n\t.global _init\n\t.global _fini\n_init:\n\tbx lr\n_fini:\n\tbx lr\n' \
        > /tmp/ndless-shim/crtaux.S
    arm-none-eabi-as -mcpu=arm926ej-s -o /tmp/ndless-shim/crtaux.o /tmp/ndless-shim/crtaux.S

Without it the link fails with `undefined reference to '_fini'`.

### 7. `zehn_loader` (needed by `make-prg` for `--compress`)

`make-prg` builds `tools/zehn_loader` on first use. That build compiles its
loader against a vendored zlib and expects `zlib.h` in the loader's build
directory, which zlib's `configure` does not copy:

    cp /tmp/ndless/ndless-sdk/thirdparty/zlib/zlib.h \
       /tmp/ndless/ndless-sdk/tools/zehn_loader/zlib/zlib.h
    make -C /tmp/ndless/ndless-sdk/tools/zehn_loader -s all

Without it packaging fails with `loader.cpp:9:10: fatal error: zlib.h: No such
file or directory`.

---

Audio pack (optional, for real sounds)
--------------------------------------

Crafti's audio engine plays an on-calculator pack, `crafti.audp`, built from the
two provided asset archives:

| archive | contents | used for |
| --- | --- | --- |
| `sounds_trimmed.zip` | `extracted/**/*.ogg` (1103 files) | all sound effects |
| `minecraft-essentials-music.zip` | `music/*.mp3` (8 files) | music tracks |

It needs `ffmpeg` on `PATH`:

    sudo apt-get install -y ffmpeg

Unpack both archives (the script expects the layout they already have, so no
renaming or flattening is needed), then build the pack:

    mkdir -p audiosrc && cd audiosrc
    unzip -q /path/to/sounds_trimmed.zip          # -> extracted/**/*.ogg
    unzip -q /path/to/minecraft-essentials-music.zip   # -> music/*.mp3
    cd ..

    python3 tools/audio/build_audio_pack.py \
        --sounds audiosrc/extracted \
        --music  audiosrc/music \
        --out    crafti.audp \
        --header audio_sounds.h

This writes `crafti.audp` (~10.7 MB, **1111 sounds** = 1103 effects + 8 music,
8-bit unsigned mono 8 kHz) and regenerates `audio_sounds.h` with the id table
(1112 entries: `None = 0` plus ids 1..1111, contiguous and in the same order as
the pack index, so the engine needs no runtime name lookup).

Sanity check the result before copying it, so a partially decoded archive cannot
slip through. The three counts must agree:

    # sound ids in the generated header (1..1111, so 1111 lines with a comment)
    grep -cE '^ *[A-Za-z0-9_]+ *= *[0-9]+, *//' audio_sounds.h    # 1111
    # pack sound count, read straight from the AUD1 header
    python3 -c "import struct;print(struct.unpack('<4sHHHHIII',open('crafti.audp','rb').read(24))[4])"   # 1111
    # and the sources: 1103 .ogg + 8 .mp3 in the two archives

Copy the pack to the calculator next to the game:

    /documents/ndless/crafti.audp      # or /documents/crafti.audp

Without a pack the game still runs and falls back to procedural tones.
`crafti.audp` is gitignored; the source asset licensing is unresolved, so do not
redistribute it. See `GPIO4_AUDIO_TEST.md` for the format, the GPIO4 output
backend and the audio test mode.

---

Notes
-----

- `tests/` builds against SDL on the desktop and is not part of the CX target.
  The desktop build uses `Makefile.pc`. Run the host tests (audio, livestock,
  village generation) with `make -C tests`.
- GPIO4 audio is opt-in because it takes over the interrupt vector and the fast
  timer (Settings → GPIO4 audio, or the Audio Test screen). See
  `GPIO4_AUDIO_TEST.md`; it still needs real CX hardware validation.
- `LIVESTOCK.md` covers the passive animals, `VILLAGE.md` the procedural
  villages and villagers. Village frequency is a setting (Settings → Villages)
  and, like terrain, is baked into chunk data, so it only affects chunks
  generated after the change.
- The Makefile finds sources with `find` and has no header dependency tracking,
  so after changing a **header** an incremental `make` can leave stale objects
  behind. LTO usually reports this as a `-Wodr` / `-Wlto-type-mismatch` warning.
  Run `make clean && make` (or `touch` the affected `.cpp` files) after header
  changes.
- If build tools are missing, check your Ndless SDK/toolchain installation and
  `PATH` first.
