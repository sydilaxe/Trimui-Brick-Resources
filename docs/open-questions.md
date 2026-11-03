# Open questions

Things the sources disagree on or nobody has tested. When you settle one on a device, write the
answer into the right doc, note the device and firmware version, and remove it here.

| # | Question | Why it matters | How to settle it |
| --- | --- | --- | --- |
| 1 | Is the **Brick Hammer's** LED layout the Brick's 14 (top bar 1-8, F2 9, F1 10, L1 L2 R2 R1 11-14)? | The 14-LED map was measured on a plain Brick | `tools/led_probe.sh walk 23` on a Hammer ([brick-hammer-mapping.md](brick-hammer-mapping.md)) |
| 2 | Does `frame_hex` block on a second write on the **Hammer** (as on the Brick) and not on the **Pro**? | A blocked write hangs the writing thread in the kernel | On a Hammer: write `frame_hex` twice without toggling `effect_enable`, from a second SSH session you can kill |
| 3 | Brick Pro **right ring** (15-18) and named `l`/`r` zones: RRGGBB or GGRRBB? MustardOS's `rgb.sh` swaps R and G there; the sydilaxe apps don't | Red and green come out swapped on one side | `tools/led_probe.sh colour 15` on a Brick Pro |
| 4 | Does the Brick Pro have a `rear` zone (`effect_rear`, `max_scale_rear`) for the shoulder LEDs? NextUI says so | A simpler way to light the shoulders | `tools/led_probe.sh zones` / `show` on a Brick Pro |
| 5 | Is `max_scale` really capped at 60? LED'oh! reports 80+ working on the Brick | Brighter LEDs | Compare 60 and 100 on each device, by eye |
| 6 | TrimUI OS Brick Pro: is button 15 the HOME key? (NextUI calls it `MENU_ALT`) | HOME as an app button on TrimUI OS | `tools/joydump` on TrimUI OS, Brick Pro |
| 7 | Side switch on the Brick / Hammer: same GPIO 243 and same polarity (0 up, 1 down) as the Pro under MustardOS? | Apps use it as a mode switch | `tools/joydump` prints the switch as it changes |
| 8 | Brick Pro USB-C: which port is the host port? | MIDI and USB audio | Plug in a MIDI device; `ls /dev/snd` / `cat /proc/asound/cards` |
| 9 | MustardOS Brick / Hammer: are the volume keys joystick buttons (like the Pro's 2 and 3) or only seen by MustardOS? | Avoid reading volume as another button | `tools/joydump` on MustardOS, Hammer |
| 10 | Brick Pro triggers L2/R2: analog or digital? Spec sheets disagree | Expressive triggers | `tools/evdump` (ABS events = analog) on TrimUI OS |
