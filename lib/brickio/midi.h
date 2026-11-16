/* SPDX-License-Identifier: GPL-3.0-or-later */
/* USB MIDI for the TrimUI Brick / Brick Pro (part of brickio, made to be shared by the sydilaxe
 * TrimUI apps). Reads ALSA's raw MIDI devices (/dev/snd/midiC*D*), so no libasound to ship; works
 * alongside USB audio on the same port or hub.
 *
 *   - a reader thread: each message is parsed the moment it arrives (running status, SysEx skipped)
 *     and queued with its time; the app is woken (wake callback) so it can act at once rather than
 *     at its next screen frame
 *   - devices plugged in later are found (it looks every two seconds); ones pulled out are dropped
 *   - notes, CCs, program change, pitch bend, aftertouch, and the clock / start / stop /
 *     continue real-time messages (for tempo sync)
 *   - midi_send: bytes out to every device that takes them (clock out, for instance)
 * Costs: a sleeping thread, woken only by MIDI and the two-second look for devices. */
#pragma once
#include <stdint.h>

enum { MIDI_CC, MIDI_NOTE, MIDI_PROGRAM, MIDI_BEND, MIDI_TOUCH, MIDI_CLOCK, MIDI_START, MIDI_CONTINUE, MIDI_STOP };

typedef struct {
    uint8_t type; /* MIDI_* */
    uint8_t ch;   /* 0..15 (channel messages) */
    uint8_t note; /* 1 for a note (num = note; val = velocity, 0 = off), else 0 (kept for older code) */
    uint8_t num;  /* CC number, note, program; pitch bend: the low 7 bits */
    uint8_t val;  /* CC value, velocity, pressure; pitch bend: the high 7 bits */
    uint64_t t_ns;/* when it was read (CLOCK_MONOTONIC) */
} MidiEv;

/* starts the reader thread. pattern: another device glob (tests: a FIFO), or NULL;
 * wake: called from the thread when messages are queued (e.g. push an SDL user event), or NULL */
int midi_start(const char *pattern, void (*wake)(void));
/* the messages queued since the last call, up to max (any thread, one reader) */
int midi_poll(MidiEv *out, int max);
/* bytes out to every open device that takes them; how many devices took them */
int midi_send(const uint8_t *bytes, int n);
/* how many devices are open, and the first one's name ("" if none) */
int midi_count(void);
const char *midi_name(void);
void midi_stop(void);
#define midi_close midi_stop
