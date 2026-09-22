# Multimedia — codecs, decode paths and how they are verified

## Three separate questions, never merged

| layer | question | source |
|---|---|---|
| **silicon** | does this GPU have the fixed-function block? | capability DB in `hardware/hwcap.c`, keyed by PCI id |
| **runtime** | does *this boot* expose it? | a real `vdpauinfo` / `vainfo` query (`ml_hwdec_probe`) |
| **measured** | did a decode actually use it? | a real encode+decode round trip (`ml_video_test_run`) |

`ml_codec_capability()` returns `ML_CAP_HW` **only** when silicon and runtime
both advertise the codec. Silicon yes + no runtime query = `not verified`
(NOT TESTED) with the reason spelled out — this is the state on a machine where
`vdpauinfo` is not installed, and it is deliberately not rounded up to a PASS:

    H.264: ATI Radeon HD 4670 (RV730) has it in silicon but no vdpauinfo query
           ran here — NOT TESTED

## Hardware decode attribution (the strict rule)

A decode is called hardware-accelerated only when the decoder's **own output**
names a hardware decoder on its stream/decoder line — `vdpau (h264_vdpau)`,
`h264_vaapi`, `h264_cuvid`, `_qsv`, `_v4l2m2m`, ... — and no failure banner
(`No usable`, `not supported`, `Failed to`, `Cannot load`, ...) appears in the
same log. `decode_used_hardware()` in `hardware/media.c` implements exactly that.

The earlier, weaker rule (looking for the word `hwaccel` anywhere) was removed:
`ffmpeg -hwaccel auto` echoes the option whether or not a hardware path engaged,
so that rule could report acceleration that never happened. This is the class of
false claim the spec forbids, so the check is now strict and there is a veto list.

## What `maclite-video-test` reports

    maclite-video-test                        # h264 + mpeg2 at 720p
    maclite-video-test h264 mpeg2 --res 1080  # 1080p
    maclite-video-test h264 --sw              # force the software path (comparison)
    maclite-video-test h264 --perf --seek 2   # seek latency + A/V decode skew

Per codec, one of four honest verdicts:

| verdict | meaning |
|---|---|
| `PASS` | a hardware decoder really engaged (decoder line names it) |
| `FAIL` | the silicon has the block but the decode ran in software |
| `PARTIAL` | decoded in software; hardware support unknown or absent — plays, but not accelerated |
| `NOT TESTED` | nothing could run (no ffmpeg/mpv/gstreamer on PATH) — never PASS |

The playback performance test (`--perf`) measures real things on a real file
built by ffmpeg (`testsrc2` video + `sine` audio): the wall time from `-ss` to
the **first decoded frame after a seek**, and the decode time of the video and
audio streams over the same timeline.

**A/V sync, honestly stated:** decoding two streams and comparing their
completion times measures *decode skew*, which is what a compositor-side sync
bug shows up as. It is **not** a lip-sync measurement — that needs a clock, an
output sink and a human. The tool says this in its own output, and
`scripts/hardware-check.sh` keeps "audio and video stay in sync" as a human row
whose unanswered state is `NOT TESTED`.

## Playback in the OS itself

`mica-player` uses the same capability knowledge as the tools: it asks the
capability DB and the runtime probe, prefers a hardware decode path when one is
actually advertised, and falls back to software. There is no GStreamer pipeline,
no Electron/HTML5 media stack and no bundled codec library in MacLiteOS — the
decoder is whatever the system provides (`ffmpeg`, `mpv`), and when none is
present `mica-player` says so instead of failing silently.

## Verification status

| item | status |
|---|---|
| capability DB lookups, codec name/bit mapping, normalisation | fixture- and unit-verified |
| `ml_codec_capability()` states (HW / SW / unverified) | fixture-verified |
| strict hardware-decode attribution | code-reviewed; **NOT TESTED** end-to-end (no ffmpeg here) |
| 720p / 1080p H.264 decode, hardware vs software | **NOT TESTED** |
| MPEG-2 decode (the other codec iMac11,2 supports) | **NOT TESTED** |
| seek latency, A/V decode skew | **NOT TESTED** |
| fullscreen playback smoothness | software-path numbers exist (`maclite-gpu-benchmark --fullscreen`, 4.39 ms worst frame at 1080p); **real playback NOT TESTED** |

To fill these in, on the iMac:

    maclite-video-test h264 mpeg2 --res 1080 --perf
    maclite-video-test h264 mpeg2 --res 720 --perf
    maclite-video-test h264 --sw --res 1080      # comparison run
    maclite-video-test h264 --perf --seek 30     # seek into the middle

and paste the numbers into `docs/testing.md`. Prefer hardware, fall back to
software, and record which one you got — not which one you hoped for.
