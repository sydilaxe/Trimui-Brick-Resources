/* USB MIDI on the TrimUI (the USB host port is the Brick's top USB-C),
 * through ALSA's raw MIDI ports (/dev/snd/midiC*D*): nothing else is needed
 * on the device under MustardOS. Ports are found as they are
 * plugged in (a rescan each second) and dropped when they go away.
 *
 * From the sydilaxe apps' midi.c, without their engines: a thread of its own
 * (SCHED_FIFO 40, below the sound, above the screen, where allowed) reads
 * every port and hands each complete message to a callback, with its arrival
 * time. System realtime bytes (clock F8, start FA, continue FB, stop FC) come
 * through as one-byte messages even in the middle of another. SysEx is
 * skipped.
 *
 *   bm_start(on_msg, user, -1);
 *   bm_send(bytes, n);          // to every port, from any thread
 *   bm_stop();
 *
 * The callback runs on the MIDI thread: keep it short, and hand work to the
 * audio or main thread through a lock-free queue or atomics. */
#pragma once
#include <stdint.h>

typedef void (*BmCallback)(const uint8_t *msg, int len, uint64_t t_us, void *user);

/* core: pin the thread to this core (-1: anywhere). 1 if it started. */
int bm_start(BmCallback cb, void *user, int core);
void bm_stop(void);
void bm_send(const uint8_t *b, int n);
/* the next "MIDI: <device>" / "MIDI: <device> GONE" notice, main thread; 0 if none */
int bm_take_notice(char *buf, int n);
int bm_port_count(void);
uint64_t bm_now_us(void); /* CLOCK_MONOTONIC, the clock the times use */
