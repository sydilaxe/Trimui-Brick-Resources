# Hardware

## The three devices

The **Brick Hammer** is a Brick variant: MustardOS treats it as `tui-brick` (product id 2), and the
sydilaxe apps ship one build for both **[muOS] [app]**. Its LED layout is assumed to match the Brick's;
see [open-questions.md](open-questions.md) and [brick-hammer-mapping.md](brick-hammer-mapping.md).

| | Brick / Brick Hammer | Brick Pro |
| --- | --- | --- |
| SoC | Allwinner A133P: 4x Cortex-A53, PowerVR GE8300 **[web]** | same |
| Clock | 408 MHz - 2.0 GHz in MustardOS's config (sold as 1.8 GHz) **[muOS] [web]** | same |
| RAM / storage | 1 GB, 8 GB eMMC + microSD **[web]** | 1 GB, 8 GB eMMC + microSD **[web]** |
| Screen | 3.2" IPS, 1024x768, 60 Hz **[web] [muOS]** | 3.95" IPS, 1024x768, 60 Hz, laminated **[web] [muOS]** |
| HDMI out | 1280x720 in MustardOS's config **[muOS]** | same |
| Battery | 3000 mAh **[muOS]** | 5000 mAh **[muOS]** |
| Sticks | none | two Hall-effect sticks, clickable **[web] [app]** |
| Buttons | d-pad, A B X Y, L1 L2 R1 R2, SELECT START MENU, F1 F2, volume, power, side switch | the same plus HOME, L3 R3 |
| LEDs | 14 addressable RGB **[LED'oh]** | 22 addressable RGB (23 slots) **[app] [muOS]** |
| Speaker | mono **[muOS]** | mono **[muOS]** |
| Audio out | 3.5 mm headphone jack | same |
| USB | two USB-C: **top = host** (MIDI, audio, hubs), bottom = charging **[app]** | check on the device |
| Wi-Fi | xradio (`xradio_wlan`), `wlan0` **[muOS]** | same |
| Bluetooth | yes (4.2) **[muOS] [web]** | same |
| Rumble | GPIO 227 **[muOS]** | GPIO 227, limit ~2.5 V **[NextUI]** |
| Kernel | Linux 4.9.191 (Allwinner BSP) **[muOS]** | same |

The USB host port supplies about 500 mA; use a powered hub for anything hungrier **[app]**.

## The software side

- `aarch64` glibc userland; build for glibc 2.28 to run on both firmwares **[app]**.
- Root everywhere: apps can write sysfs, use `SCHED_FIFO`, `mlockall` **[app]**.
- SDL2 is provided by the firmware: TrimUI OS in `/usr/trimui/lib`, MustardOS in
  `/opt/muos/frontend/lib` **[app] [muOS]**. Link against a stub and use theirs.
- The screen is `/dev/fb0`, drawn through SDL2's renderer (GLES on the PowerVR, or software).

## Sysfs and device paths

| Path | What | Source |
| --- | --- | --- |
| `/sys/class/led_anim/` | the RGB LEDs, see [leds.md](leds.md) | [app] |
| `/sys/class/gpio/gpio243/value` | side switch: 0 up, 1 down (export 243 if missing) | [app] |
| `/sys/class/gpio/gpio227/value` | rumble motor | [muOS] |
| `/sys/class/power_supply/axp2202-battery/capacity` | battery % | [muOS] |
| `/sys/class/power_supply/axp2202-battery/voltage_now` | battery voltage | [muOS] |
| `/sys/class/power_supply/axp2202-usb/online` | charger connected | [muOS] |
| `/sys/class/power_supply/axp2202-battery/work_led`, `lowpwr_led` | the charge/power LEDs | [muOS] |
| `/sys/class/power_supply/axp2202-battery/mcu_pwr` | power to the LED MCU (serial LED backend) | [muOS] |
| `/sys/class/graphics/fb0/blank` | screen blank: 4 off, 0 on | [app] |
| `/sys/class/disp/disp/attr/color_temperature` | screen colour temperature | [muOS] |
| `/sys/class/speaker/mute` | speaker mute (TrimUI OS) | [NextUI] |
| `/sys/devices/system/cpu/cpu[0-3]/online`, `cpufreq/scaling_governor` | cores and governor | [app] |
| `/sys/class/thermal/thermal_zone0/temp` | SoC temperature (milli-°C) | [app] |
| `/dev/snd/midiC*D*` | USB MIDI ports (ALSA raw MIDI) | [app] |
| `/proc/asound/cards` | sound cards (USB ones say `USB-Audio`) | [muOS] |
| `/dev/rtc0` | real-time clock | [muOS] |
| `/dev/ttyS5` | serial LED backend MustardOS supports on other boards; the Brick uses sysfs | [muOS] |
| `mmcblk0` / `mmcblk1` | internal eMMC (boot, root, ROMS partitions) / SD card | [muOS] |

## Firmware identification

| Check | Brick / Hammer | Brick Pro |
| --- | --- | --- |
| `/opt/muos/device/config/board/name` (MustardOS) | `tui-brick` | `tui-brick-pro` |
| `$DEVICE` (TrimUI OS) | `brick` | `brickpro` |
| `$TRIMUI_MODEL` (TrimUI OS) | names the model | names the model |
| `/opt/muos/config/system/version` | MustardOS version | |
| `/etc/version`, `/usr/trimui/version` | TrimUI OS version | |
