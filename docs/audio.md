# Audio

## The sound path

| | TrimUI OS | MustardOS |
| --- | --- | --- |
| Server | ALSA directly | PipeWire + WirePlumber **[muOS]** |
| Built-in card | `audiocodec` (card 0, `sndcodec`) | same, as `alsa_output._sys_devices_platform_soc_sndcodec_sound_card0.playback.0.0` **[muOS]** |
| Speaker | mono | mono (`audio/channel` = `Mono`) **[muOS]** |
| Headphones | 3.5 mm, stereo | same |
| Volume | firmware | `wpctl set-volume @DEFAULT_AUDIO_SINK@ N%` (`script/device/audio.sh`) **[muOS]** |
| USB audio | ALSA card | PipeWire sink; `script/device/usbaudio.sh` switches to it **[muOS]** |

SDL2 reaches PipeWire on MustardOS either through its PipeWire driver or ALSA's PipeWire plugin. Log
`SDL_GetCurrentAudioDriver()` at start so you know which **[app]**.

MustardOS's environment (from `/opt/muos/script/var/base.sh`, loaded by `SETUP_APP`) **[muOS]**:

```
XDG_RUNTIME_DIR=/run
PIPEWIRE_RUNTIME_DIR=/run
ALSA_CONFIG=/usr/share/alsa/alsa.conf
LD_LIBRARY_PATH=/opt/muos/frontend/lib:...
```

`wpctl`, `pw-top` and `pw-metadata` need `XDG_RUNTIME_DIR=/run` when run from SSH.

MustardOS's WirePlumber settings for the Brick (`device/tui-brick*/control/wireplumber.conf`) keep
sinks running (`node.pause-on-idle = false`, `node.always-process = true`, no suspend) with
`api.alsa.period-size = 256`, `periods = 3`, `headroom = 128`, `disable-batch = true`, and turn on
ALSA MIDI in PipeWire (`alsa.midi = true`) **[muOS]**.

## Settings that work **[app]**

- **Format**: `AUDIO_S16SYS`, 2 channels, 48000 Hz. Let SDL convert if the device wants otherwise
  (pass `0` for `allowed_changes` to `SDL_OpenAudioDevice`).
- **Buffer**: 512 frames (~10.7 ms) is the default in Columbia Street, HI5 and Jaffedrone; 1024 in
  RadioScan (it decodes streams). Offer `<APP>_BUFFER=1024` for crackles and `<APP>_RATE=44100`.
- **Render ahead**: make the sound on your own thread into a ring buffer and let the SDL callback
  only copy out. The callback's timing on PipeWire is uneven.
- **Real-time priority**: `SCHED_FIFO` for the render thread (Jaffedrone uses 45 for the voices;
  MIDI 40, "below the sound, above the screen"). Running as root, this is allowed. Make it switchable
  (`<APP>_RT=0`).
- **Pin threads** with `pthread_setaffinity_np`: Jaffedrone puts the voices on core 2 and the effects
  on core 3, a block behind, leaving cores 0-1 to the screen, system and MIDI. The busiest core does
  ~40% less than one core doing all.
- **Lock memory**: `mlockall(MCL_CURRENT | MCL_FUTURE)` so audio never waits on a page fault (1 GB of
  RAM is plenty for that).
- **Flush denormals to zero** on every audio thread. AArch64:

  ```c
  uint64_t fpcr;
  __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
  fpcr |= 1u << 24;                       /* FZ */
  __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
  ```

  Decaying reverb and filter tails otherwise cost many times the CPU.
- **Governor**: in `launch.sh`, bring all four cores online and set `performance`, then put the old
  governor back after:

  ```sh
  CPU=/sys/devices/system/cpu
  gov_was=$(cat $CPU/cpu0/cpufreq/scaling_governor)
  for c in 0 1 2 3; do echo 1 > $CPU/cpu$c/online 2>/dev/null; done
  echo performance > $CPU/cpu0/cpufreq/scaling_governor
  ...
  echo "$gov_was" > $CPU/cpu0/cpufreq/scaling_governor
  ```

  MustardOS's defaults: 408 MHz - 2.0 GHz, `ondemand` **[muOS]**. The A133P is sold as 1.8 GHz **[web]**.
- **Floats and long delays**: a float sample counter loses precision past 2^24 samples (5.8 min at
  48 kHz). Keep delay positions wrapped within their buffer **[app]**.
- **A stuck slow device**: a driver can get stuck feeding at half speed after a hiccup. Watch the
  callback rate every couple of seconds and reopen the device if it has clearly fallen behind
  **[app]**.

## Measuring

The sydilaxe apps write a `log.txt` with a STAT line every 10 s, e.g.

```
STAT  audio load avg 9% peak 21% | dropouts 0, late calls 0, jumps 0 | gap max 11.0 ms (buffer 10.7) | 94 calls/s | screen 30 fps ...
```

Over ~60% audio load, crackles are near. A click with no dropout or JUMP in the app's log comes from
PipeWire: `pw-top -b` shows each node's ERR (xrun) count. [`tools/sysinfo.sh`](../tools/sysinfo.sh)
records the sound path, PipeWire's graph and xruns every 10 s while the app runs.

## USB audio **[app]**

- The **top** USB-C port is the host port; the bottom one charges. It gives about 500 mA; use a powered
  hub for hungry devices.
- Class-compliant interfaces and DACs work. Pick one by name: look for `usb`, `uac` or `dac` in
  `SDL_GetAudioDeviceName(i, 0)`.
- On `SDL_AUDIODEVICEADDED` / `SDL_AUDIODEVICEREMOVED`, pick again and reopen. Copy the device name
  before reopening (SDL's list changes underneath it). Fall back to the default device if the open
  fails.
- Give users `<APP>_AUDIO=internal` and `<APP>_AUDIO=<part of a name>`.

## USB MIDI **[app]**

- ALSA raw MIDI: scan `/dev/snd` for `midiC*D*` and open them `O_RDWR | O_NONBLOCK`; rescan every
  second or two to catch hot-plugged devices.
- Run MIDI on its own thread (`SCHED_FIFO` 40), `poll()` on the open ports.
- Clock out: 24 per quarter note, plus START/STOP, timed to when the sound is **heard** (add the
  output latency, about one buffer).
- Clock in: follow tempo and hold phase by trimming the playback rate slightly.
- MustardOS's PipeWire also opens ALSA MIDI (`alsa.midi = true`); raw MIDI still works alongside it in
  the apps.

## Headphone jack tricks **[app]**

HI5 and Columbia Street have a MONO+CLK output: the mix in mono on one side of the headphone jack and
clock pulses on the other, to sync a Pocket Operator or Volca.
