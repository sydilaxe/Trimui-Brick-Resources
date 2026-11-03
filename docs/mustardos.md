# MustardOS (muOS)

## Installing an app

- A **`.muxapp`** is a zip holding one folder, `<Name>/`. The user copies it into `ARCHIVE/` at the
  root of the first SD card and installs it from **Applications > Archive Manager**. It lands in
  `MUOS/application/<Name>/`; the folder name is the name in the Applications list **[app]**.
- For development: copy the folder to `MUOS/application/<Name>/` on the card, or over SSH
  (MustardOS: **Web Services > Secure Shell**; the user is `root`) **[app]**.

## The app folder **[app]**

```
<Name>/
  mux_launch.sh      MustardOS runs this (header comments set help, icon, name)
  launch.sh          the same launcher the TrimUI OS package uses
  wake_screen.sh     wakes the Brick Pro's screen (see below)
  glyph/<icon>.svg   list icon (80x80, white strokes on transparent)
  <binary>, data, ...
```

`mux_launch.sh` header **[app]**:

```sh
#!/bin/sh
# HELP: My App: one line shown in the list's help. MENU quits.
# ICON: myapp          <- glyph/myapp.svg
# GRID: My App         <- the name in grid view
```

Then set up the app the way MustardOS's own apps are set up:

```sh
APP_DIR="${1:-$(dirname "$0")}"      # MustardOS passes the app folder
if [ -f /opt/muos/script/var/retro.sh ]; then
    . /opt/muos/script/var/retro.sh
    command -v SETUP_APP >/dev/null 2>&1 && SETUP_APP myapp ""
elif [ -f /opt/muos/script/var/func.sh ]; then   # older muOS
    . /opt/muos/script/var/func.sh
    echo app >/tmp/act_go
    command -v SETUP_SDL_ENVIRONMENT >/dev/null 2>&1 && SETUP_SDL_ENVIRONMENT
    command -v SET_VAR >/dev/null 2>&1 && SET_VAR "system" "foreground_process" "myapp"
fi
cd "$APP_DIR" || exit 1
export MYAPP_SCREEN_WAKE="$PWD/wake_screen.sh"
./launch.sh
```

`SETUP_APP <process> [..]` **[muOS]**: tells the frontend an app is running (`app` to `$ACT_GO`),
applies the governor in `/run/muos/governor`, sets `HOME` and `XDG_CONFIG_HOME`, records the
foreground process name (use your binary's name: MustardOS uses it to manage the process), and runs
`SETUP_SDL_ENVIRONMENT` (GameController DB, `SDL_HQ_SCALER`, `SDL_ROTATION`,
`SDL_BLITTER_DISABLED`, a cursor preload).

Full template: [`templates/mustardos/`](../templates/mustardos).

## Black screen fixes

### 1. Alpha **[app]**

MustardOS's display **uses the alpha channel**. A software surface created with no alpha mask and
copied to an ARGB8888 texture goes out with alpha 0 and shows **black**. TrimUI OS ignores alpha, so
the bug only shows on MustardOS. Fix: set alpha to 0xFF on every pixel before `SDL_UpdateTexture`, and
`SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE)`.

```c
for (int y = 0; y < H; y++) {
    Uint32 *row = (Uint32 *)((Uint8 *)surf->pixels + y * surf->pitch);
    for (int x = 0; x < W; x++) row[x] |= 0xFF000000u;
}
```

### 2. The Brick Pro's screen stays dark **[app]**

On the Brick Pro, after an app opens its window, the screen can stay dark (the app runs and plays)
until a sleep/wake. `wake_screen.sh` does the waking half: blank and unblank the framebuffer and
re-apply the saved brightness, twice, a second apart:

```sh
echo 4 > /sys/class/graphics/fb0/blank; echo 0 > /sys/class/graphics/fb0/blank
echo 0 > /tmp/fb_blank; rm -f /tmp/mux_blank
/opt/muos/script/device/bright.sh "$(cat /opt/muos/config/settings/general/brightness)" transient
```

Run it from the app ~10 frames after the window is up, in the background (double fork, never
waited for). Then **rebuild the SDL renderer and texture** ~2.5 s later, and whenever
`SDL_RENDER_TARGETS_RESET` / `SDL_RENDER_DEVICE_RESET` arrives or a draw call fails, because blanking
can take the renderer's picture with it.

**Quote the path**: `MUOS/application/Columbia Street/wake_screen.sh` has a space. Columbia Street's
first MustardOS build ran it unquoted through `sh -c` and the wake never ran.

## Useful paths **[muOS]**

| Path | Holds |
| --- | --- |
| `/opt/muos/device/config/board/name` | `tui-brick` or `tui-brick-pro` |
| `/opt/muos/device/config/` | everything below, as one file per value |
| `.../board/sdl_map` | the GameController mapping line |
| `.../board/stick` | number of sticks: 0 Brick, 2 Pro |
| `.../board/rumble` | `/sys/class/gpio/gpio227/value` |
| `.../led/rgb` | 1 (has RGB LEDs) |
| `.../audio/*` | sink names, mono speaker, default volume |
| `.../cpu/*` | 4 cores, 408000-2000000 kHz, ondemand tunables |
| `.../screen/*` | 1024x768 internal, 1280x720 HDMI out, `/dev/fb0`, refresh 60.0 |
| `/opt/muos/config/settings/general/brightness` | the screen brightness setting |
| `/opt/muos/config/settings/rgb/` | the user's LED settings |
| `/opt/muos/config/system/version` | MustardOS version |
| `/opt/muos/script/device/` | `rgb.sh`, `bright.sh`, `audio.sh`, `usbaudio.sh`, `headphone.sh` |
| `/opt/muos/script/var/` | `retro.sh` (`SETUP_APP`), `base.sh`, `display.sh` |
| `/opt/muos/frontend/lib` | MustardOS's libs (SDL2 among them), on `LD_LIBRARY_PATH` |
| `/run/muos/` | run-time state |

## What else differs from TrimUI OS

- Buttons: its own `muOS-Keys` controller with different numbers ([input.md](input.md)).
- Sound: PipeWire ([audio.md](audio.md)).
- LEDs: brightness starts at 0 ([leds.md](leds.md)).
- The side switch is free for apps; MustardOS ignores it.
- Volume keys are MustardOS's; don't act on them.
- MustardOS doesn't set `DEVICE`/`TRIMUI_MODEL`; read the board name.

## Self-updating apps **[app]**

The sydilaxe apps update from their GitHub releases. Each build fetches its own asset: the `.muxapp`
when `mux_launch.sh` is in its folder, else the TrimUI OS zip. Keep `mux_launch.sh` out of the TrimUI
OS zip so the test stays true. Exit with status 75 after updating and have `launch.sh` loop on 75 to
start the new version.
