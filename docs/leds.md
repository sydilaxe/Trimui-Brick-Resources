# LEDs: `/sys/class/led_anim`

The Brick and Brick Pro LEDs are driven by TrimUI's `led_anim` kernel driver. Both TrimUI OS and
MustardOS expose it at `/sys/class/led_anim`, writable by root. **[app] [muOS] [NextUI]**

There are two ways to drive them:

- **Named zones** (`effect_*`): a few groups of LEDs, each with one colour and a hardware effect
  (static, breathe, blink...). Simple and stable.
- **`frame_hex`**: every LED on its own, one colour each. Needed for meters and stick rings.
  The two are mutually exclusive: `frame_hex` only shows while `effect_enable` is `0`.

## Physical layout

### Brick / Brick Hammer: 14 LEDs **[LED'oh] [app]**

Measured on a Brick by teardown. The Hammer is assumed to match; confirm it with
[brick-hammer-mapping.md](brick-hammer-mapping.md).

```
FRONT (screen facing you)

  Top bar:   [1] [2] [3] [4] [5] [6] [7] [8]     <- left ... right
  Buttons:   [F1 = 10]               [F2 = 9]    <- the two lit buttons under the screen

BACK / shoulders
  [L1 = 11] [L2 = 12]           [R2 = 13] [R1 = 14]
```

| `frame_hex` position (1-based) | LED | Named zone |
| --- | --- | --- |
| 1-8 | top bar, left to right | `m` |
| 9 | F2 | `f2` |
| 10 | F1 | `f1` |
| 11 | L1 | `lr` |
| 12 | L2 | `lr` |
| 13 | R2 | `lr` |
| 14 | R1 | `lr` |
| 15-23 | nothing | |

Note **F2 comes before F1**, and the shoulders run L1, L2, R2, R1 (outside in on the left, inside out
on the right).

### Brick Pro: 22 LEDs used, 23 slots **[app] [muOS]**

```
FRONT
  Top bar:     [1] [2] [3] [4] [5] [6] [7] [8]
  Buttons:     [F1 = 10]                 [F2 = 9]
  Stick rings: left 11-14                right 15-18
               each ring: upper-left, lower-left, lower-right, upper-right

BACK / shoulders
  [L1 = 19] [L2 = 20]             [R2 = 21] [R1 = 22]
```

| `frame_hex` position (1-based) | LED |
| --- | --- |
| 1-8 | top bar, left to right |
| 9 | F2 |
| 10 | F1 |
| 11 | left ring, upper left |
| 12 | left ring, lower left |
| 13 | left ring, lower right |
| 14 | left ring, upper right |
| 15 | right ring, upper left |
| 16 | right ring, lower left |
| 17 | right ring, lower right |
| 18 | right ring, upper right |
| 19 | L1 |
| 20 | L2 |
| 21 | R2 |
| 22 | R1 |
| 23 | unused (write `000000`) |

MustardOS's `rgb.sh` agrees on positions 15-22: it writes the right stick as two arcs, "RS1" at
15-16 and "RS2" at 17-18, and the shoulders at 19-22 in the order L1, L2, R2, R1. **[muOS]**

On the Brick Pro the named zones `l`, `r` and `lr` only reach the **left** stick's ring
**[app]**. NextUI also lists a `rear` zone (with `max_scale_rear`) for the Pro's shoulder LEDs
**[NextUI] [unverified]**. Use `frame_hex` to drive everything on the Pro.

### Colour byte order **[unverified on Pro]**

- The driver takes `RRGGBB` for every LED on the Brick **[LED'oh] [app]**.
- On the Brick Pro, MustardOS's `rgb.sh` writes the **right ring (15-18)** and the named `l`/`r`
  zones in **GGRRBB** order (it swaps red and green: "L/R channels are swapped on tui-brick-pro").
  The sydilaxe apps write plain `RRGGBB` there. One of them is wrong for the right ring. Check with
  `tools/led_probe.sh` (see [open-questions.md](open-questions.md)) before relying on red vs green
  there. Blue is the same either way.
- The Smart Pro's logo LED is GRB (not a Brick device, for reference) **[LED'oh]**.

## The sysfs files

All values are written as text with a trailing newline. The driver's help asks for a trailing
**space** after hex colours (`"FF0000 "`); MustardOS writes `"FF0000 \n"`, Columbia Street writes
`"FF0000\n"` and both work **[app] [muOS]**. Writing `"RRGGBB \n"` satisfies both.

| File | Values | Does |
| --- | --- | --- |
| `max_scale` | 0-60 | Brightness of the top bar (`m`) on the Brick and Pro |
| `max_scale_lr` | 0-60 | Brightness of `lr`: Brick shoulders, Pro left ring |
| `max_scale_f1f2` | 0-60 | Brightness of F1 and F2 (there is no separate `max_scale_f2`) |
| `max_scale_rear` | 0-60 | Pro only, shoulders **[NextUI] [unverified]** |
| `effect_enable` | 0 / 1 | 1: the named zone effects run. 0: they stop and `frame_hex` shows |
| `effect_<z>` | effect number | Writing it (re)starts the effect: write the colour first, then this |
| `effect_rgb_hex_<z>` | `RRGGBB ` | Zone colour |
| `effect_duration_<z>` | ms | Effect period (breath, blink) |
| `effect_cycles_<z>` | -1, 0, n | -1 loops forever, 0 stops, n plays n times |
| `frame_hex` | 23 x `RRGGBB ` | Every LED's colour; shows while `effect_enable` is 0 |
| `effect_names` | read | The driver's list of effect names |
| `help` | read | The driver's own help (written for the Smart Pro; its LED count is wrong for the Brick) |
| `anim_frames*` | | The driver's frame animation; "Did not finish yet" in its help: don't use |

`<z>` is a zone: `m` (top bar), `f1`, `f2`, `lr` (both sides together), `l`, `r`.

The brightness ceiling is 60 in the driver's help ("tg5040 limit brightness 60") and MustardOS
clamps to 60 **[muOS]**. LED'oh! reports values above 60 working on the Brick **[LED'oh]**. Stay at or
below 60.

Brightness multiplies the colour: `frame_hex` LEDs still need their `max_scale*` above 0 to show.

### Effect numbers **[muOS] [NextUI]**

| `effect_<z>` | Effect |
| --- | --- |
| 0 | off / stop |
| 1 | linear |
| 2 | breathe (set the speed with `effect_duration`, e.g. 3000 / 5000 / 10000 ms) |
| 3 | sniff |
| 4 | static (steady colour) |
| 5 | blink 1 |
| 6 | blink 2 |
| 7 | blink 3 |

For a steady colour that changes often (a beat flash, a level), write the colour and then `4` each
time: the new colour only shows once the effect is written again **[app]**.

## What the firmware does with the LEDs

- **MustardOS starts with the LED brightness at 0**, so an app that only writes colours shows
  nothing. Set `max_scale`, `max_scale_lr`, `max_scale_f1f2` yourself (and put them back after).
  **[app]**
- MustardOS's own LED settings live in `/opt/muos/config/settings/rgb/` (`mode`, `bright`,
  `colour_f1`, `colour_m`, `colour_rs1`, `colour_shl1`...) and are applied by
  `/opt/muos/script/device/rgb.sh` **[muOS]**. Your app's changes don't touch these files, so
  restoring the sysfs values you found is enough.
- `rgb.sh` usage, handy from SSH:
  `/opt/muos/script/device/rgb.sh <mode 1-9> <brightness 0-255> <L r g b> [R r g b] [M r g b] [F1 r g b] [F2 r g b] ...`
  (modes: 1 static, 2-4 breathe fast/med/slow, 5-7 blink, 8 linear, 9 sniff) **[muOS]**.
- NextUI keeps its LED settings in `ledsettings_brick.txt` / `ledsettings_brickpro.txt` **[NextUI]**.
- The charge/power LEDs are separate: `/sys/class/power_supply/axp2202-battery/work_led` and
  `lowpwr_led` **[muOS]**.

## Hazard: `frame_hex` writes can block

On the **original Brick**, LED'oh! found that a second `frame_hex` write while `effect_enable` is 0
**blocks in the kernel** (uninterruptible; Ctrl+C does nothing) **[LED'oh]**. Their workaround, per
frame:

```sh
echo "RRGGBB ... " > /sys/class/led_anim/frame_hex   # the frame
echo 1 > /sys/class/led_anim/effect_enable           # releases the driver's lock
echo 0 > /sys/class/led_anim/effect_enable           # frame_hex shows again
```

Set every zone colour to `000000` first, so nothing flashes during the moment `effect_enable` is 1.

The sydilaxe apps write `frame_hex` on the **Brick Pro** every 25-33 ms without the toggle and have
not hit this **[app]**, so the Pro's driver seems not to have the problem. On the original Brick
they use the named zones instead. Either way:

- write LED files from a thread of their own (never the audio callback, never the render loop
  if you can avoid it),
- write only when the frame changed,
- if you use `frame_hex` on the Brick, use the toggle.

## The pattern the apps use **[app]**

1. Check the driver is there and writable: `access("/sys/class/led_anim/frame_hex", W_OK)`.
2. Save every zone's `effect_rgb_hex_<z>` and `effect_<z>`, each `max_scale*`, and `effect_enable`.
   Write them to a file in the app folder too (`leds_saved.txt`). If that file already exists at
   start, the last run crashed: restore from the file rather than from sysfs.
3. Set brightness (60 by default; let the user lower it with an env var).
4. Brick Pro: `effect_enable 0`, then write `frame_hex`. Brick: `effect_enable 1`,
   `effect_cycles_<z> -1`, then colour + `4` per zone.
5. On exit: `frame_hex` all `000000`, then each zone's colour, then its effect (writing it restarts
   it), then the brightness values, then `effect_enable`. Delete the saved file.
6. Give users a way out: `<APP>_LEDS=0` leaves the LEDs alone.

The code is in [`src/ringleds.c`](../src/ringleds.c) (Brick Pro, `frame_hex`) and
[`src/brick_zones.c`](../src/brick_zones.c) (Brick / Hammer, named zones). Both are used by
[`examples/skeleton`](../examples/skeleton).

## Ideas that read well on these LEDs **[app]**

- **Stereo level meter on the top bar**: 4 LEDs per channel from the centre out, -42..0 dB, green,
  green, amber, red; peak hold that falls by x0.82 per ~33 ms frame.
- **Stick rings**: light each corner by `x*cx + y*cy` where the corners are (-1, 1), (-1, -1),
  (1, -1), (1, 1), so the ring points where the stick is pushed.
- **Beat on the shoulders**: a flash on each quarter note that fades over the beat, a different
  colour on the bar's first beat.
- **F1 / F2** as state: mute scene on, voice level, a short white flash on press.
- Quantise brightness to eighths with a gamma of about 1.8 (`pow(round(t*8)/8, 1.8)`): smooth to the
  eye and few writes.
