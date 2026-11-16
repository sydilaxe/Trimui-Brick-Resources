#!/bin/sh
# What the Brick's bottom port can be as a USB device: run on the Brick (as root, over SSH), with no app
# running, and send the output. It decides how a receiving Brick shows itself to the primary one for
# Brick-to-Brick audio: a standard USB audio device (uac2 / uac1), or brickio's own audio channel on
# the clock link (FunctionFS, which the apps already use).
#
#   sh probe-gadget.sh > probe.txt 2>&1
#
# It changes nothing that stays: it reads the kernel's files, and makes a test gadget
# (/sys/kernel/config/usb_gadget/brickioprobe) that is never bound to the port and is removed at once.
# Then probe-iso (built from tools/probe-iso.c, next to this script) tries isochronous endpoints:
# that one binds its test gadget to the port for about a second, as the apps' clock link does at
# every start, so unplug the bottom port while it runs.
CFG=/sys/kernel/config/usb_gadget
G=$CFG/brickioprobe

echo "== the system"
uname -a
cat /proc/device-tree/model 2>/dev/null; echo
[ -d /opt/muos ] && echo "MustardOS" || echo "not MustardOS"
id

echo "== the device controllers (the bottom port)"
ls /sys/class/udc 2>&1
for u in /sys/class/udc/*; do [ -e "$u" ] && echo "$(basename "$u"): state $(cat "$u/state" 2>/dev/null), speed $(cat "$u/current_speed" 2>/dev/null), max $(cat "$u/maximum_speed" 2>/dev/null)"; done

echo "== gadgets set up now (an app's clock link shows here as trimuilink)"
ls "$CFG" 2>&1

echo "== the kernel's USB gadget functions (built in or as modules)"
if [ -r /proc/config.gz ]; then
    zcat /proc/config.gz | grep -E "CONFIG_USB_(CONFIGFS|F_|G_|LIBCOMPOSITE|AUDIO|FUNCTIONFS|MIDI)" | grep -v "^#"
else
    echo "(no /proc/config.gz)"
fi
find /lib/modules/"$(uname -r)" -name "usb_f_*" -o -name "libcomposite*" -o -name "g_*.ko*" 2>/dev/null
lsmod 2>/dev/null | grep -E "usb_f|composite|gadget"

echo "== trying each function in a test gadget (never bound to the port)"
if [ ! -d "$CFG" ]; then
    echo "no configfs gadget support here"
    exit 0
fi
mkdir "$G" 2>/dev/null || { echo "can't make a test gadget (not root?)"; exit 0; }
# each step says what it is about to do first, so a step that hangs shows in probe.txt.
# Not ffs: the apps' clock link already uses it, and a bare FunctionFS instance made and removed
# without being mounted hung a Brick's kernel (4.9) on its removal. probe-iso tests it the way the
# clock link sets it up and takes it down (mounted, then unmounted, then removed).
for f in uac2 uac1 midi; do
    if mkdir "$G/functions/$f.probe" 2>/dev/null; then
        echo "$f: AVAILABLE"
        echo "  removing the $f test..."
        rmdir "$G/functions/$f.probe" && echo "  removed" || echo "  couldn't remove it"
    else
        echo "$f: not available"
    fi
done
echo "removing the test gadget..."
rmdir "$G" 2>/dev/null
[ -d "$G" ] && echo "note: $G is still there; remove it with: rmdir $G" || echo "test gadget removed"

echo "== isochronous endpoints (what a standard USB audio device needs)"
# probe-iso binds a test gadget to the port for a moment (as the apps' clock link does at every start):
# unplug the bottom port first
here=$(dirname "$0")
if [ -x "$here/probe-iso" ]; then
    "$here/probe-iso"
else
    echo "(no probe-iso next to this script)"
fi
