# Input: buttons, sticks, triggers, switch

Under MustardOS the buttons show up in SDL2 as its `muOS-Keys` joystick, numbered differently on
the Brick and the Brick Pro, so detect which one you have at run time. Code:
[`src/brick_input.h`](../src/brick_input.h), which reads only muOS-Keys. TrimUI OS's own numbering is
kept below for reference only: the apps no longer build for TrimUI OS or read its controller.

The Brick Hammer reads exactly as the Brick (same MustardOS board `tui-brick`, product id 2, same
builds in the sydilaxe apps) **[app] [muOS]**.

## Use the raw joystick, not the GameController API

MustardOS ships two GameController mappings per device, `modern.txt` and `retro.txt`
(`/opt/muos/device/control/gamecontrollerdb/`), which swap A/B and X/Y, and sets
`SDL_GAMECONTROLLERCONFIG` from the user's choice in `SETUP_SDL_ENVIRONMENT` **[muOS]**. Through
`SDL_GameController`, "A" moves when the user changes that setting. Raw `SDL_JoystickOpen` button
numbers never move, so the buttons stay as labelled **[app]**.

## Which controller is it?

| Controller | Name | Vendor (GUID bytes 4-5) | Product (GUID bytes 8-9) |
| --- | --- | --- | --- |
| MustardOS, Brick / Hammer | `muOS-Keys` | `0x756d` ("mu") | 2 |
| MustardOS, Brick Pro | `muOS-Keys` | `0x756d` | 3 |
| TrimUI OS | TrimUI's own gamepad | other | other |

Full GUIDs: Brick `060000006d750000020000006f730000`, Brick Pro `060000006d750000030000006f730000`
**[muOS]**.

Read vendor and product from the GUID (`g.data[4] | g.data[5] << 8`, `g.data[8] | g.data[9] << 8`):
`SDL_JoystickGetVendor` isn't in every SDL2 build the devices ship **[app]**.

Fallbacks, in order **[app]**:

1. MustardOS: `/opt/muos/device/config/board/name` is `tui-brick` or `tui-brick-pro`.
2. Last resort: more than 17 buttons means Brick Pro.

(TrimUI OS, for reference: the `DEVICE` env var is `brick` or `brickpro` **[NextUI]**; `TRIMUI_MODEL`
names the model.)

**Under MustardOS both controllers exist**: muOS-Keys and the TrimUI gamepad underneath it. Read only
muOS-Keys, or every press arrives twice **[app]**.

Set `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` to `"1"` and handle `SDL_JOYDEVICEADDED` /
`SDL_JOYDEVICEREMOVED` **[app]**.

## Button numbers **[app] [muOS] [NextUI]**

`-` means "not a button there" (the d-pad is a hat; TrimUI OS's L2/R2 are axes).

| Button | TrimUI OS Brick | TrimUI OS Brick Pro | MustardOS Brick / Hammer | MustardOS Brick Pro |
| --- | --- | --- | --- | --- |
| D-pad | hat 0 | hat 0 | hat 0 | hat 0 |
| A | 1 | 1 | 5 | 6 |
| B | 0 | 0 | 4 | 5 |
| X | 3 | 3 | 6 | 7 |
| Y | 2 | 2 | 7 | 8 |
| L1 | 4 | 4 | 8 | 9 |
| R1 | 5 | 5 | 9 | 10 |
| L2 | axis 2 | axis 2 | 10 | 11 |
| R2 | axis 5 | axis 5 | 11 | 12 |
| SELECT | 6 | 6 | 12 | 13 |
| START | 7 | 7 | 13 | 14 |
| MENU | 8 | 8 | 14 | 15 |
| F1 (lit, under the screen) | 9 | 11 | 15 | 0 |
| F2 (lit, under the screen) | 10 | 12 | 16 | 1 |
| L3 (left stick click) | - | 9 | - | 16 |
| R3 (right stick click) | - | 10 | - | 17 |
| Volume down | 13 **[NextUI]** | 13 **[NextUI]** | handled by MustardOS | 2 |
| Volume up | 14 **[NextUI]** | 14 **[NextUI]** | handled by MustardOS | 3 |
| HOME (Pro, left, under volume) | - | 15 (NextUI's `MENU_ALT`) **[unverified]** | - | 4 |

Notes:

- On the Brick, F1/F2 are the buttons other firmwares' maps call **L3/R3** (the Brick has no sticks
  to click). NextUI calls the Pro's F1/F2 **L4/R4** **[NextUI]**.
- MustardOS's Brick Pro sends F1 and F2 as **0 and 1**, numbered before everything else, and the volume
  keys as 2 and 3. An early Columbia Street build read the volume keys as F1/F2 by mistake **[app]**.
- Leave volume to the firmware: MustardOS changes the volume itself on those keys.
- TrimUI OS's first public firmware had A/B and X/Y swapped relative to their labels; the numbers
  above are what NextUI and the apps use today **[NextUI]**.
- The power key is evdev code 116 (`KEY_POWER`), handled by the firmware **[NextUI]**.

## Axes **[app] [NextUI] [muOS]**

| Axis | TrimUI OS (Brick Pro) | MustardOS Brick Pro |
| --- | --- | --- |
| 0 | left X | left X |
| 1 | left Y | left Y |
| 2 | L2 (trigger) | right X |
| 3 | right X | right Y |
| 4 | right Y | |
| 5 | R2 (trigger) | |

- Range about -32767..32767 (NextUI says ±30k in practice); Y is negative **up**, so flip it if
  you want up = +1.
- On TrimUI OS the Brick's L2/R2 are also axes 2 and 5. MustardOS's Brick has no axes in use
  (its GameController map lists `leftx:a0, lefty:a1` but there is no stick).
- L2/R2 as buttons from an axis: down above 16384, up below 8192 (hysteresis) **[app]**.
- The Brick Pro's triggers are digital in use (spec sheets disagree on analog) **[web]**.

### Stick handling that works **[app]**

- Dead zone: 0.12-0.2 of full scale, rescaled so output starts at 0 past it.
- The sticks seldom reach their full travel: learn each direction's furthest point and treat 96% of
  it as full (start with 60% assumed).
- The gate is round: to reach both ends on a diagonal, stretch to a square (scale by `r / max(|x|,|y|)`).
- Hall sticks jitter slightly: add hysteresis to anything that steps (e.g. switch a step only when
  most of the way to the next).

## Side switch (FN switch) **[app]**

The slide switch above the power button is **GPIO 243**, on the Brick and the Brick Pro:

```
/sys/class/gpio/gpio243/value     # 0 = up, 1 = down (seen on the Brick Pro under MustardOS)
```

If `gpio243` isn't exported, export it (`echo 243 > /sys/class/gpio/export`, then `direction` =
`in`) and unexport it at exit. Poll it (it's cheap) or `poll()` on the value file.

- MustardOS's input daemon passes the switch on as a switch on muOS-Keys, but MustardOS itself
  does nothing with it: free for apps.
- On TrimUI OS, check the system's own FN switch setting: it may act on the switch too.
- Give users `<APP>_SWITCH=0` (ignore) and `<APP>_SWITCH_FLIP=1`.

## Rumble **[muOS] [NextUI]**

`/sys/class/gpio/gpio227/value` is the vibration motor (MustardOS `board/rumble`). NextUI limits
the Brick Pro's motor to 2.5 V because 3.3 V is harsh.

## Checking a firmware's numbers

Build [`tools/joydump.c`](../tools/joydump.c) and run it on the device over SSH: it lists every
controller (name, vendor, product, counts) and prints each button, hat and axis event. Or use
`evtest` where installed. Log raw button numbers in your app's log at a detail level, as the
sydilaxe apps do: it makes a new firmware's changes obvious.

Let users remap without a rebuild: `<APP>_JOYMAP=A=1,B=0,F1=9` **[app]**.
