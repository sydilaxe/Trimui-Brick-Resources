# AGENTS.md

This repo is reference material for building audio apps for the TrimUI Brick, Brick Hammer and
Brick Pro, mainly on MustardOS (muOS). It is self-contained: the docs, drop-in C modules, templates
and a working skeleton app cover what the sydilaxe app repos learned, so there is no need to dig
through those. Read [README.md](README.md) for the index, then the doc for the part you are working
on. To start a new app, copy `examples/skeleton/` and the `src/` files it uses.

## Rules that save a build

1. **Read buttons as a raw `SDL_Joystick`, not `SDL_GameController`.** MustardOS swaps A/B (and X/Y)
   in its GameController mapping depending on the user's layout setting, so the same press arrives
   as a different button. The raw numbers are fixed. Tables: [docs/input.md](docs/input.md),
   code: [src/brick_input.h](src/brick_input.h).
2. **The numbering differs per firmware and per device.** TrimUI OS's own controller and
   MustardOS's `muOS-Keys` controller use different numbers, and the Brick and Brick Pro differ under
   MustardOS. Detect at run time: muOS-Keys has vendor `0x756d`, product 2 (Brick) or 3 (Brick Pro).
   When muOS-Keys is present, ignore the other controller or every press arrives twice.
3. **The d-pad is hat 0** on every firmware. L2/R2 are axes 2 and 5 on TrimUI OS but buttons on
   MustardOS.
4. **Draw opaque pixels.** MustardOS shows the alpha channel: a surface drawn with alpha 0 shows as a
   black screen. OR `0xFF000000` into every pixel before presenting. [docs/mustardos.md](docs/mustardos.md)
5. **The Brick Pro's screen can stay dark under MustardOS** after the window opens. Run the
   `wake_screen.sh` template once the first frames are up, then rebuild the SDL renderer about 2.5 s
   later. If the app folder name has a space, quote the path when running it.
6. **LEDs: save what is there at start and put it back on exit**, and keep a copy in a file so a
   crash can be undone on the next start. MustardOS starts with LED brightness at 0, so set
   `max_scale*` yourself (1..60). [docs/leds.md](docs/leds.md)
7. **Never write LED sysfs files from the audio thread.** A `frame_hex` write can block in the kernel
   (it does on the original Brick). Write from a thread of its own, only when something changed, at
   most every 25 ms.
8. **On the original Brick use the named zones** (`f1`, `f2`, `m`, `lr`), not `frame_hex`, unless you
   use the toggle workaround in [docs/leds.md](docs/leds.md). On the Brick Pro use `frame_hex`: the
   named zones only reach the left stick's ring there.
9. **Keep the audio callback small**: 16-bit stereo at 48 kHz, 512-frame buffers (1024 if it
   crackles), memory locked with `mlockall`, flush-to-zero set on the audio threads, governor
   `performance` while running. [docs/audio.md](docs/audio.md)
10. **Follow USB audio hot-plug**: reopen the SDL audio device on `SDL_AUDIODEVICEADDED/REMOVED`.
    The top USB-C port is the host port.
11. **Build with zig** for `aarch64-linux-gnu.2.28 -mcpu=cortex_a53` against a stub `libSDL2-2.0.so.0`;
    the device's own SDL2 is loaded at run time. [docs/build.md](docs/build.md)
12. **Build only for MustardOS.** The sydilaxe TrimUI apps ship only MustardOS `.muxapp` packages
    for the Brick and Brick Pro: no TrimUI OS or Knulli builds. One binary can serve every device;
    package it as `<Name>-mustardos.muxapp` (`<Name>/` with `mux_launch.sh`).

## Keeping this repo right

- Tag every fact with its source (`[app]`, `[muOS]`, `[NextUI]`, `[LED'oh]`, `[web]`,
  `[unverified]`), as the docs do.
- When a device test confirms or disproves something, fix the doc, move the item out of
  [docs/open-questions.md](docs/open-questions.md) and note the firmware version it was tested on.
- `src/ringleds.c`, `src/perflog.c`, `tools/sysinfo.sh` and `tools/mksdlstub.py` are copies from the
  sydilaxe apps; `brick_input`, `brick_zones` and `brick_midi` are standalone versions of their code.
  If you improve one in an app, bring it back here.
- Before committing: `tests/run.sh`, `cd examples/skeleton && make package`, `cd tools && make`.
