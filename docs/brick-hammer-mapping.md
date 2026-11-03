# Brick Hammer mapping session (MustardOS)

A step-by-step checklist for mapping the Brick Hammer's LEDs and buttons on MustardOS, about 30
minutes. Fill in the blanks, then fold the results into [leds.md](leds.md) and
[input.md](input.md) and tick off [open-questions.md](open-questions.md).

## Before you start

Record:

- MustardOS version (`cat /opt/muos/config/system/version`): ________
- Board name (`cat /opt/muos/device/config/board/name`; expect `tui-brick`): ________

On a computer, build the tools:

```bash
cd tools && make
```

On the Hammer: MustardOS > **Web Services > Secure Shell** on, and note its IP (Wi-Fi settings).
Copy the tools over (user `root`):

```bash
scp tools/build/evdump tools/build/joydump tools/led_probe.sh root@HAMMER_IP:/tmp/
```

Open **two** SSH sessions (`ssh root@HAMMER_IP`). Use the second one to `killall` anything that hangs.

## 1. LED positions (`frame_hex`)

```bash
sh /tmp/led_probe.sh walk 23
```

Each Enter lights the next position in white. Write what lights (or "nothing"):

| Pos | Expected (Brick) | Hammer |
| --- | --- | --- |
| 1 | top bar, far left | |
| 2 | top bar 2 | |
| 3 | top bar 3 | |
| 4 | top bar 4 | |
| 5 | top bar 5 | |
| 6 | top bar 6 | |
| 7 | top bar 7 | |
| 8 | top bar, far right | |
| 9 | F2 | |
| 10 | F1 | |
| 11 | L1 | |
| 12 | L2 | |
| 13 | R2 | |
| 14 | R1 | |
| 15-23 | nothing | |

If a step hangs (the prompt never comes back), that's the write-block bug: in the second session
run `killall sh`, then `sh /tmp/led_probe.sh show` and note it under step 4.

## 2. Colour order

```bash
sh /tmp/led_probe.sh colour 1
sh /tmp/led_probe.sh colour 10
sh /tmp/led_probe.sh colour 11
```

| Pos | FF0000 showed | 00FF00 showed | Order (RGB / GRB) |
| --- | --- | --- | --- |
| 1 (top bar) | | | |
| 10 (F1) | | | |
| 11 (L1) | | | |

## 3. Named zones

```bash
sh /tmp/led_probe.sh zones
```

| Zone | Expected (Brick) | Hammer |
| --- | --- | --- |
| `m` | whole top bar | |
| `f1` | F1 | |
| `f2` | F2 | |
| `lr` | all four shoulders | |
| `l` | ? (Smart Pro left stick) | |
| `r` | ? (Smart Pro right stick) | |
| `rear` | probably missing | |

Also note: does `max_scale` above 60 look brighter? (`echo 100 > /sys/class/led_anim/max_scale`
while a zone is lit) ________

## 4. The write block

In session 1:

```bash
cd /sys/class/led_anim
echo 40 > max_scale; echo 0 > effect_enable
echo "FF0000 " > frame_hex
echo "00FF00 " > frame_hex      # does this return?
```

- Second write returned: yes / no (hung) ________
- If it hung: session 2 `killall sh`, then `echo 1 > /sys/class/led_anim/effect_enable`.

## 5. Buttons as SDL sees them (what apps use)

```bash
. /opt/muos/script/var/retro.sh; SETUP_SDL_ENVIRONMENT
/tmp/joydump 120
```

It prints the controllers first: note the muOS-Keys line (expect vendor `756d`, product `0002`).

________

Press each control and fill in the raw number and what `brick_input` read it as:

| Control | Expected (MustardOS Brick) | Hammer raw | Read as |
| --- | --- | --- | --- |
| D-pad up / down / left / right | hat 0 = 1 / 4 / 8 / 2 | | |
| A | 5 | | |
| B | 4 | | |
| X | 6 | | |
| Y | 7 | | |
| L1 | 8 | | |
| R1 | 9 | | |
| L2 | 10 | | |
| R2 | 11 | | |
| SELECT | 12 | | |
| START | 13 | | |
| MENU | 14 | | |
| F1 | 15 | | |
| F2 | 16 | | |
| Volume down | ? | | |
| Volume up | ? | | |
| Power (short press: careful, it may sleep) | not a joystick button | | |
| Side switch up / down | GPIO 243: 0 / 1? | | |

If something the Hammer has isn't listed (an extra button), add a row.

## 6. Buttons as the kernel sees them (optional)

```bash
/tmp/evdump
```

Note each device's name and vendor/product, and the KEY/SW codes for volume, power and the side
switch:

________

## Afterwards

- Update `docs/leds.md` (Brick Hammer section, or "same as the Brick, confirmed on Hammer, MustardOS x.y").
- Update `docs/input.md` (confirm the MustardOS Brick column for the Hammer; volume keys; switch).
- Settle open questions 1, 2, 5, 7, 9.
- Change the tags from **[LED'oh]** / **[unverified]** to **[device: Hammer, MustardOS x.y]**.
