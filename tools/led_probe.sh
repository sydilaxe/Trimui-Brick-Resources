#!/bin/sh
# Probe the led_anim LEDs on a Brick, Brick Hammer or Brick Pro, over SSH, one step at a time.
# Saves the LED settings first and puts them back on exit (also on Ctrl+C).
#
#   led_probe.sh walk [N]      light frame_hex positions 1..N (23 by default) one at a time, in white;
#                              press Enter for the next. Note what lights for each number.
#   led_probe.sh colour P      light position P red, then green, then blue (Enter between):
#                              shows whether that LED takes RRGGBB or GGRRBB
#   led_probe.sh zones         light each named zone (m, f1, f2, lr, l, r, rear) red in turn
#   led_probe.sh show          print the driver's current values
#
# frame_hex writes can block in the kernel on the original Brick when written twice in a row; this
# script toggles effect_enable 1 -> 0 before every write, which avoids it (see docs/leds.md).
# Run as root (the default SSH user on both firmwares).

D=${LED_DIR:-/sys/class/led_anim}
SAVE=/tmp/led_probe_saved.txt
ZONES="m f1 f2 lr l r rear"
SCALES="max_scale max_scale_lr max_scale_f1f2 max_scale_rear"
BRIGHT=${BRIGHT:-40}

[ -d "$D" ] || { echo "no $D here"; exit 1; }

rd() { [ -r "$D/$1" ] && head -n 1 "$D/$1" 2>/dev/null | tr -d '\n '; }
wr() { [ -w "$D/$1" ] && printf '%s\n' "$2" > "$D/$1" 2>/dev/null; }

save() {
    : > "$SAVE"
    for z in $ZONES; do
        printf 'effect_rgb_hex_%s=%s\n' "$z" "$(rd effect_rgb_hex_$z)" >> "$SAVE"
        printf 'effect_%s=%s\n' "$z" "$(rd effect_$z)" >> "$SAVE"
    done
    for s in $SCALES effect_enable; do printf '%s=%s\n' "$s" "$(rd $s)" >> "$SAVE"; done
}

restore() {
    [ -f "$SAVE" ] || return
    frame_all 000000
    # colours first, then effects (writing an effect restarts it), then brightness, then effect_enable
    grep '^effect_rgb_hex_' "$SAVE" | while IFS='=' read -r k v; do [ -n "$v" ] && wr "$k" "$v "; done
    grep -E '^effect_(m|f1|f2|lr|l|r|rear)=' "$SAVE" | while IFS='=' read -r k v; do [ -n "$v" ] && wr "$k" "$v"; done
    grep '^max_scale' "$SAVE" | while IFS='=' read -r k v; do [ -n "$v" ] && wr "$k" "$v"; done
    v=$(grep '^effect_enable=' "$SAVE" | cut -d= -f2); [ -n "$v" ] && wr effect_enable "$v"
    rm -f "$SAVE"
    echo "LED settings put back"
}

# a frame of 23 colours: the arguments, then 000000 for the rest
frame() {
    f=""; n=0
    for c in "$@"; do f="$f$c "; n=$((n + 1)); done
    while [ $n -lt 23 ]; do f="${f}000000 "; n=$((n + 1)); done
    wr effect_enable 1; wr effect_enable 0    # clears the driver's write block
    printf '%s\n' "$f" > "$D/frame_hex"
}
frame_all() { # every position in one colour
    c=$1; f=""; i=0
    while [ $i -lt 23 ]; do f="$f$c "; i=$((i + 1)); done
    wr effect_enable 1; wr effect_enable 0
    printf '%s\n' "$f" > "$D/frame_hex"
}
one() { # position (1-based), colour
    p=$1; c=$2; args=""; i=1
    while [ $i -le 23 ]; do [ $i -eq "$p" ] && args="$args $c" || args="$args 000000"; i=$((i + 1)); done
    # shellcheck disable=SC2086
    frame $args
}

prepare_frame() {
    for z in $ZONES; do wr effect_rgb_hex_$z "000000 "; wr effect_$z 4; done
    for s in $SCALES; do wr "$s" "$BRIGHT"; done
    wr effect_enable 0
    sleep 0.3
}

wait_enter() { printf '%s  [Enter] ' "$1"; read -r _; }

case "$1" in
walk)
    save; trap 'restore; exit' INT TERM; prepare_frame
    N=${2:-23}; p=1
    while [ $p -le "$N" ]; do one $p FFFFFF; wait_enter "position $p lit (white)"; p=$((p + 1)); done
    restore ;;
colour | color)
    [ -n "$2" ] || { echo "usage: $0 colour <position 1-23>"; exit 1; }
    save; trap 'restore; exit' INT TERM; prepare_frame
    one "$2" FF0000; wait_enter "position $2 sent FF0000 (red if RRGGBB, green if GGRRBB)"
    one "$2" 00FF00; wait_enter "position $2 sent 00FF00 (green if RRGGBB, red if GGRRBB)"
    one "$2" 0000FF; wait_enter "position $2 sent 0000FF (blue either way)"
    restore ;;
zones)
    save; trap 'restore; exit' INT TERM
    for s in $SCALES; do wr "$s" "$BRIGHT"; done
    wr effect_enable 1
    for z in $ZONES; do wr effect_rgb_hex_$z "000000 "; wr effect_$z 4; done
    for z in $ZONES; do
        if [ ! -w "$D/effect_rgb_hex_$z" ]; then echo "zone $z: no such file"; continue; fi
        wr effect_rgb_hex_$z "FF0000 "; wr effect_$z 4
        wait_enter "zone $z lit red"
        wr effect_rgb_hex_$z "000000 "; wr effect_$z 4
    done
    restore ;;
show)
    for f in $SCALES effect_enable; do echo "$f: $(rd $f)"; done
    for z in $ZONES; do echo "zone $z: colour '$(rd effect_rgb_hex_$z)' effect '$(rd effect_$z)'"; done
    ls "$D" ;;
*)
    sed -n '2,15p' "$0" ;;
esac
