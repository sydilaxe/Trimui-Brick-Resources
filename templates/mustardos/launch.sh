#!/bin/sh
# The app's launcher, run by MustardOS through mux_launch.sh. Keeps all four cores online at full speed while the app
# runs, and puts the governor back after.
APP=myapp
cd "$(dirname "$0")"

CPU=/sys/devices/system/cpu
gov_was=$(cat $CPU/cpu0/cpufreq/scaling_governor 2>/dev/null)
for c in 0 1 2 3; do
    [ -f $CPU/cpu$c/online ] && echo 1 > $CPU/cpu$c/online 2>/dev/null
done
echo performance > $CPU/cpu0/cpufreq/scaling_governor 2>/dev/null

# MustardOS's SETUP_APP has already put its own SDL2 first
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/usr/lib"

# Optional settings (remove the # to use one; the prefix is the app's own):
# export SK_BUFFER=1024        # bigger audio buffers if it crackles (512 by default)
# export SK_RATE=44100         # a lower sample rate: a little less work
# export SK_AUDIO=internal     # always the speaker / headphones (default: a USB device when plugged in)
# export SK_JOYMAP=A=1,B=0     # another firmware's button numbers
# export SK_LEDS=0             # leave the LEDs alone
# export SK_LED_BRIGHTNESS=30  # 1..60
# export SK_LOG=2              # performance log every second, and every button press

[ "${SYSINFO:-1}" != 0 ] && [ -x ./sysinfo.sh ] && ./sysinfo.sh start "$APP"

# exit status 75: the app updated itself, start the new version
while :; do
    ./"$APP" > log.txt 2>&1
    [ $? -eq 75 ] || break
done

[ -x ./sysinfo.sh ] && ./sysinfo.sh stop "$APP"
[ -n "$gov_was" ] && echo "$gov_was" > $CPU/cpu0/cpufreq/scaling_governor 2>/dev/null
