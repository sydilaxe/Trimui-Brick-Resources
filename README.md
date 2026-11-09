# TrimUI Brick / Brick Pro: resources for audio app builds

Hardware facts that are hard to find elsewhere, for agents and people building audio apps for the
TrimUI **Brick**, **Brick Hammer** and **Brick Pro**, mainly on **MustardOS** (muOS), with TrimUI OS
notes where they differ.

Most of this was taken from code that already runs on the devices: the four sydilaxe TrimUI apps
(Columbia Street, HI5, RadioScan, Jaffedrone), MustardOS's own device files and scripts
(`MustardOS/internal`), NextUI's `tg5040` platform code, and the LED'oh! project's teardown notes.
Each fact is tagged with where it came from (see [Sources](#sources-and-confidence)).

## Start here

| You need | Read |
| --- | --- |
| The rules to follow and the mistakes to avoid | [AGENTS.md](AGENTS.md) |
| The LEDs: sysfs files, zones, effects, the per-LED maps | [docs/leds.md](docs/leds.md) |
| The buttons, sticks, triggers, F1/F2, HOME, side switch | [docs/input.md](docs/input.md) |
| Sound: PipeWire, buffers, USB audio, USB MIDI, threads | [docs/audio.md](docs/audio.md) |
| MustardOS: packaging, launch script, black screen fixes | [docs/mustardos.md](docs/mustardos.md) |
| Specs, sysfs paths, GPIOs, USB ports | [docs/hardware.md](docs/hardware.md) |
| Cross-compiling with zig, linking SDL2, CI | [docs/build.md](docs/build.md) |
| Things nobody has confirmed yet | [docs/open-questions.md](docs/open-questions.md) |
| Mapping a Brick Hammer by hand | [docs/brick-hammer-mapping.md](docs/brick-hammer-mapping.md) |

## Drop-in code (no need to open the app repos)

Everything here builds as is: `cd examples/skeleton && make package` gives a MustardOS `.muxapp`
for all three devices (the apps build only for MustardOS) (the skeleton itself has not been run on hardware yet;
the code it is built from ships in the sydilaxe apps). `tests/run.sh` runs the host tests.

| File | What it is |
| --- | --- |
| [`examples/skeleton/`](examples/skeleton) | A working app: screen (opaque, MustardOS wake), input, 48 kHz audio with USB hot-plug, LEDs, USB MIDI, log; plus its Makefile (zig, SDL2 stub, the `.muxapp`) |
| [`src/brick_input.c`](src/brick_input.c), [`.h`](src/brick_input.h) | Buttons, sticks and side switch for all four firmware/device pairs, detected at run time |
| [`src/ringleds.c`](src/ringleds.c), [`.h`](src/ringleds.h) | Brick Pro: all 22 LEDs via `frame_hex` (level meter, stick rings, beat), saved and restored |
| [`src/brick_zones.c`](src/brick_zones.c), [`.h`](src/brick_zones.h) | Brick / Hammer: the named LED zones (F1, F2, top bar, shoulders), saved and restored |
| [`src/brick_midi.c`](src/brick_midi.c), [`.h`](src/brick_midi.h) | USB MIDI through ALSA raw MIDI: hot-plug, parser, send to all ports |
| [`src/perflog.c`](src/perflog.c), [`.h`](src/perflog.h) | Performance log: audio load, dropouts, clicks, CPU, with tuning hints |
| [`templates/mustardos/`](templates/mustardos) | `mux_launch.sh`, `launch.sh` (governor, update loop), `wake_screen.sh`, a list glyph |
| [`third_party/SDL2-2.30.9-headers/`](third_party/SDL2-2.30.9-headers) | SDL2 headers to build against |
| [`tools/mksdlstub.py`](tools/mksdlstub.py) | Makes the link-time SDL2 stand-in |
| [`tools/sysinfo.sh`](tools/sysinfo.sh) | System and sound-path report, sampled while the app runs |
| [`tools/led_probe.sh`](tools/led_probe.sh) | On the device: light LEDs one by one, check colour order, try each zone; restores after |
| [`tools/joydump.c`](tools/joydump.c), [`tools/evdump.c`](tools/evdump.c) | On the device: print SDL joystick events (and how `brick_input` reads them) / raw kernel input events. `cd tools && make` |
| [`docs/brick-hammer-mapping.md`](docs/brick-hammer-mapping.md) | A checklist for mapping a Brick Hammer's LEDs and buttons by hand |

## The devices at a glance

| | Brick / Brick Hammer | Brick Pro |
| --- | --- | --- |
| SoC | Allwinner A133P, 4x Cortex-A53, PowerVR GE8300 | same |
| RAM | 1 GB | 1 GB |
| Screen | 3.2" 1024x768 | 3.95" 1024x768 |
| Battery | 3000 mAh | 5000 mAh |
| Analog sticks | none | two Hall-effect sticks, clickable |
| Addressable LEDs | 14 (measured on a Brick; Hammer to confirm) | 22 used (driver takes 23) |
| MustardOS board name | `tui-brick` | `tui-brick-pro` |
| muOS-Keys product id | 2 | 3 |
| TrimUI OS `DEVICE` | `brick` | `brickpro` |
| Speaker | mono | mono |

## Sources and confidence

Facts carry one of these tags:

- **[app]**: used in a released sydilaxe TrimUI app and working on hardware.
- **[muOS]**: read from MustardOS's source (`MustardOS/internal`, `device/tui-brick*`, `script/`).
- **[NextUI]**: read from NextUI's `workspace/tg5040` platform code.
- **[LED'oh]**: from the LED'oh! project's Brick teardown (`ericreinsmidt/nextui-ledoh`).
- **[web]**: from spec sheets and reviews.
- **[unverified]**: sources disagree or nobody has tested it. See [docs/open-questions.md](docs/open-questions.md).

When you confirm something on a device, update the tag and the open questions list.
