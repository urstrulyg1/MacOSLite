# Audio — the smallest stack that can actually make a sound

## Architecture (and what was deliberately not built)

    maclite-audio / Settings / mica-music
              |
        hardware/audio.c            <- enumeration, mixer, tone, WAV, PCM write
              |
        <sound/asound.h>  +  ioctl(2)  +  /dev/snd/pcmC0D0p
              |
        snd-hda-intel  ->  Realtek ALC889 (iMac11,x)

* **No sound server.** No PipeWire, no PulseAudio, no JACK, no daemon of any
  kind. The mixer and PCM devices are driven with the kernel UAPI directly:
  `SNDRV_CTL_IOCTL_ELEM_*` for controls, `SNDRV_PCM_IOCTL_*` plus a plain
  `write(2)` for playback.
* **No libasound.** `<sound/asound.h>` ships with the C library headers, so this
  adds no link-time dependency at all. `./configure` reports whether libasound
  exists but nothing uses it.
* **No resampling, no effects, no mixing.** If a stream is not 48 kHz stereo
  s16, MacLiteOS does not silently convert it; the tone generator produces
  exactly what the device is asked to accept, and `ml_audio_play()` fails loudly
  when the hardware refuses the parameters.
* **No polling.** `ml_audio_play()` writes and waits on the PCM fd
  (`SNDRV_PCM_IOCTL_WRITEI_FRAMES`/`DRAIN`), never in a timer loop.

The whole audio path is ~500 lines of C including the tone generator and the WAV
writer.

## What the tools report

    maclite-audio list        cards, codecs, PCM nodes, every mixer control name
    maclite-audio volume      current playback volume as a percentage
    maclite-audio volume 50   set, verified by reading the control back
    maclite-audio mute|unmute switch state, verified the same way
    maclite-audio test --ms 1000 --hz 440        play a real generated tone
    maclite-audio test --wav /tmp/tone.wav      write the same tone to a WAV

The tone is generated in-process (`ml_tone_generate`) and written to the PCM
device; `--wav` writes a 44-byte canonical RIFF/WAVE container that
`tests/test_hardware.c` checks byte for byte. `maclite-audio test` returning 0
means *a PCM device accepted and drained real samples* — it does not mean a
human heard them, and the docs never claim that it does
(`scripts/hardware-check.sh` asks the human separately).

## Honesty rules (the ones that were bugs)

**A mixer read that could not be performed is `NOT TESTED`, never `FAIL`.**
Reading a control needs an ioctl on the real `/dev/snd/controlC0`. Under fixture
roots that file is a stub, so `ml_audio_status_get()` (in `hardware/audio.c`)
returns `readable = false` and both `maclite-audio list` and `maclite-hardware`
report the volume/mute rows as `NOT TESTED` with the reason. On real hardware an
unreadable mixer *is* a `FAIL`. This rule lives in exactly one place, because
the two tools disagreeing about the same machine is itself a bug (found by the
fixture suite; see `docs/testing.md`).

**No card is not a failure of the machine.** With no `/proc/asound` entries at
all, `maclite-audio list` reports `ALSA present = UNSUPPORTED` and exits 3 —
"there is no sound hardware here", not "audio is broken".

**A silent tone is reported as a refusal.** `ml_audio_test_tone()` returns
`ML_AUDIO_NO_DEVICE` when the PCM node is missing, and `maclite-audio test`
prints `UNSUPPORTED (no ALSA PCM node; nothing was played)` and exits 3.

## The ALC889 on iMac Mid-2010

The HDA codec exposes `Master`/`PCM`/`Headphone` playback volume and a playback
switch, plus an HDMI PCM device on the same card. `ml_audio_ctl_find_volume()`
tries, in order: `Master`, `PCM`, `Headphone`, `Speaker`, `Front` — the first
`"<base> Playback Volume"` that exists wins, and the matching
`"<base> Playback Switch"` provides mute. If none exists the tool says so.
Volume is stored as a raw control value and converted with the control's own
`min`/`max`, so the percentage matches `alsamixer` rather than a guess.

There is no software mixer: volume changes are hardware control writes. That is
why `maclite-audio volume 50` reads the value back before printing it.

## Verification status

| item | status |
|---|---|
| card/PCM enumeration from a fixture `/proc/asound` | fixture-verified (`tests/test_hardware.c`) |
| control-name search, tone sample values, WAV container | unit-verified |
| playback on a real ALC889 | **NOT TESTED** (no `/proc/asound` in the dev container) |
| HDMI audio path | **NOT TESTED** |
| capture | enumerated only; **NOT TESTED** |
| volume/mute write + read-back on hardware | **NOT TESTED** |

Run `maclite-audio test` on the iMac and record the result in
`docs/testing.md`; the human-heard part is one of the checklist rows in
`scripts/hardware-check.sh`.
