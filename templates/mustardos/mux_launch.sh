#!/bin/sh
# HELP: Skeleton: an audio app starting point. MENU quits.
# ICON: skeleton
# GRID: Skeleton

# MustardOS starts an app with this script, passing the app's folder. It sets up the app the way
# MustardOS's own apps are set up, then runs launch.sh.
APP=skeleton

APP_DIR="${1:-$(dirname "$0")}"

if [ -f /opt/muos/script/var/retro.sh ]; then
    . /opt/muos/script/var/retro.sh
    command -v SETUP_APP >/dev/null 2>&1 && SETUP_APP "$APP" ""
elif [ -f /opt/muos/script/var/func.sh ]; then
    # older muOS releases
    . /opt/muos/script/var/func.sh
    echo app >/tmp/act_go
    command -v SETUP_SDL_ENVIRONMENT >/dev/null 2>&1 && SETUP_SDL_ENVIRONMENT
    command -v SET_VAR >/dev/null 2>&1 && SET_VAR "system" "foreground_process" "$APP"
fi

cd "$APP_DIR" || exit 1
# the Brick Pro's screen can stay dark once the window opens: the app runs this then
export SK_SCREEN_WAKE="$PWD/wake_screen.sh"
./launch.sh
