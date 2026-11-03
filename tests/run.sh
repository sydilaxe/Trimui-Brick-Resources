#!/bin/sh
# Host tests: builds the LED drivers for this computer and runs them against a stand-in
# /sys/class/led_anim folder, as the device's driver would be found by an app (MustardOS's
# brightness at 0, a user's colours and effects).
set -e
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/led_anim"
for z in f1 f2 m lr l r; do
    echo "123456 " > "$work/led_anim/effect_rgb_hex_$z"
    echo 2 > "$work/led_anim/effect_$z"
    echo -1 > "$work/led_anim/effect_cycles_$z"
done
for s in max_scale max_scale_lr max_scale_f1f2; do echo 0 > "$work/led_anim/$s"; done
echo 1 > "$work/led_anim/effect_enable"
: > "$work/led_anim/frame_hex"
cc -std=gnu11 -O1 -Wall -Wextra -I"$here/../src" -o "$work/led_test" "$here/led_test.c" \
    "$here/../src/brick_zones.c" "$here/../src/ringleds.c" -lm -lpthread
(cd "$work" && ./led_test "$work/led_anim")
cc -std=gnu11 -O1 -Wall -Wextra -o "$work/midi_test" "$here/midi_test.c" -lpthread
"$work/midi_test"
