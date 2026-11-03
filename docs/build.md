# Building

## Cross-compiling with zig **[app]**

zig's bundled clang and glibc stubs make a device binary on any Linux, macOS or CI runner, with no
toolchain to install:

```sh
pip install ziglang==0.14.1          # or a zig 0.14.x on the path
python3 -m ziglang cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a53 -O2 ...
```

- `aarch64-linux-gnu.2.28`: glibc 2.28 runs on both TrimUI OS and MustardOS.
- `-mcpu=cortex_a53`: the A133P's cores.
- `-s` strips the binary.

## Linking SDL2 without the device's library **[app]**

The firmware supplies `libSDL2-2.0.so.0`. Build against the SDL2 2.30.9 headers in
`third_party/SDL2-2.30.9-headers` and link against a **stub**: a shared library with every SDL
function your code calls, as an empty function, and the right soname. The real one is loaded at
run time.

```make
build/sdl2/libSDL2-2.0.so.0: tools/mksdlstub.py $(SRC)
	python3 tools/mksdlstub.py build/sdl2/sdl2_stub.c $(SRC)
	$(ZIG) cc -target aarch64-linux-gnu.2.28 -shared -Wl,-soname,libSDL2-2.0.so.0 -o $@ build/sdl2/sdl2_stub.c

app: $(SRC) build/sdl2/libSDL2-2.0.so.0
	$(ZIG) cc -target aarch64-linux-gnu.2.28 -mcpu=cortex_a53 -O2 -Ithird_party/SDL2-2.30.9-headers \
	    -o $@ $(SRC) build/sdl2/libSDL2-2.0.so.0 -lm -lpthread -Wl,--allow-shlib-undefined -s
```

`mksdlstub.py` finds `SDL_Xxx(` calls in the sources (macros like `SDL_LoadBMP` expand to their real
functions). Only call SDL functions the devices' SDL2 builds have: avoid anything newer than about
SDL 2.26 unless you check for it. `SDL_JoystickGetVendor` is the known trap: read the GUID instead.

Include `<SDL2/SDL.h>` (the headers live in an `SDL2/` folder).

For a desktop build, `sdl2-config --cflags` gives `-I/usr/include/SDL2`; strip the `/SDL2` so
`<SDL2/SDL.h>` resolves:
`$(shell sdl2-config --cflags | sed 's|-I\([^ ]*\)/SDL2|-I\1|')`.

## Other libraries **[app]**

- Static-link anything else (Mbed TLS for HTTPS in the sydilaxe apps: compile its `library/*.c` with
  the same zig target into a `.a`). The firmwares' other shared libraries differ.
- GCC 15 on the desktop stopped zeroing whole unions for `= {0}`; Mbed TLS 3.6.2 relies on it. Add
  `-fzero-init-padding-bits=unions` when the compiler accepts it.
- `minimp3.h` (single header) decodes MP3 streams fine on the A53s (RadioScan).

## One binary or two

- **One** binary can serve every device: detect the Brick Pro at run time
  ([input.md](input.md)). RadioScan does this.
- Columbia Street, HI5 and Jaffedrone build **two** (`-DXX_BRICKPRO` adds the sticks), mainly so the
  self-updater fetches the right package. Detecting at run time is simpler.

## Packaging

See [`examples/skeleton/Makefile`](../examples/skeleton/Makefile) (`make package`):

- TrimUI OS: `<Name>-trimui.zip` holding `Apps/<Name>/` (binary, `launch.sh`, `config.json`, data).
  Unzip at the card's root.
- MustardOS: `<Name>-mustardos.muxapp`, a zip holding `<Name>/` (the same files plus
  `mux_launch.sh`, `wake_screen.sh`, `glyph/<icon>.svg`). Copy into `ARCHIVE/`, install from the
  Archive Manager.
- MustardOS's Archive Manager refuses absolute paths, `..` and links: check with
  `unzip -Z1` / `unzip -Zl` in CI **[app]**.
- Never put `mux_launch.sh` in the TrimUI OS zip.

## CI **[app]**

The sydilaxe apps build in GitHub Actions on `ubuntu-latest`: `pip install ziglang==0.14.1`,
`sh -n` every shell script, `make package VERSION=<tag>`, check the archives' contents with `unzip -l`
and `file` (`ARM aarch64`), run host tests, and publish the zip and `.muxapp` to the release on a
`v*` tag.

## Running on the device

- SSH as `root` (TrimUI OS: turn on its SSH server; MustardOS: Web Services > Secure Shell).
- `scp` the folder to `/mnt/SDCARD/Apps/<Name>/` (TrimUI OS) or the card's
  `MUOS/application/<Name>/` (MustardOS), or `make install SD=/path/to/mounted/card`.
- Read `log.txt` and `sysinfo.txt` in the app folder afterwards.
