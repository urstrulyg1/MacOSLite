# MacLiteOS architecture

MacLiteOS is a **userspace operating system shell**: a damage-tracking
compositor + window manager + supervisor in one small process, and a family of
tiny native clients (shell roles, apps, tools) that talk to it over a unix
socket with shm window buffers. The kernel is a curated Linux (kernel/configs),
not a fork: 2010 hardware needs mature radeon/b43/tg3/HDA drivers, and our
value is in the 60 fps, ~0 % idle desktop on top of them.

## Process map (crash isolation, spec §14)

    mica-comp            compositor + WM + session supervisor + notifications
      ├─ mica-shell --role panel     menu bar, menus, control center, clock
      ├─ mica-shell --role dock      magnifying dock
      ├─ mica-shell --role desktop   desktop icons + context menu
      └─ apps: mica-finder, mica-terminal, mica-viewer, mica-settings,
               mica-sysinfo, mica-textedit, mica-player, mica-music, mica-pdf

If any child dies, the supervisor respawns it (tested: kill -9 a shell role and
the desktop returns within a frame or two). If mica-comp dies, clients exit
cleanly with "compositor connection lost" — a watchdog in boot/init re-invokes
the session on real hardware.

## IPC (compositor/proto.h, core/src/ipc.c)

16-byte header + payload over SOCK_STREAM; window pixels in shm fds passed with
SCM_RIGHTS. Messages are parsed by a **non-blocking buffered watcher**: one
epoll event drains everything queued (recvmsg MSG_DONTWAIT), so a slow client
can never park the compositor in recv() — that exact bug cost us a 120 s hang
and is now a regression test target. Server→client: WELCOME, WIN_OK, INPUT,
CONFIGURE, FRAME, WIN_STATE, EVENT, PONG, STATS.

## Rendering pipeline

- Clients draw into their shm surface with the ml_ctx raster (vector font,
  rounded rects, gaussian shadows, radial glows, icons) and mark damage.
- Commit sends ≤32 damage rects; the compositor unions them with its own
  (notifications, cursor, dock magnification) and paints **only those rects**:
  wallpaper blit → windows back-to-front → notifications → cursor.
- Overlapping damage rects are merged (region simplify) because overlapping
  clips double-paint: measured first frame fell from 358 ms to 64 ms.
- No blur, no particles, no animated wallpaper. Shadows are precomputed
  gradients; glows are cubic-falloff radials.

### The pointer is a composited layer, and its damage has two sides

The pointer is not a window and not a hardware sprite: it is composited into
`C.fb` by the same pass as everything else, then the whole buffer is pushed to
scanout. That makes one rule load-bearing:

> **Every pointer move invalidates the rectangle it moved *from* as well as the
> one it moved *to*.** Damage-only scanout cannot know the pointer moved unless
> it is told to repaint where it used to be.

Miss the old rectangle and the previous pointer stays in the framebuffer
forever: on G1OS that was the reported "cursor trails / multiple cursors", and
it only got worse the longer the pointer moved, because each present left
another remnant behind. Nothing else in the pipeline can recover from it — the
wallpaper blit, the window pass and the notification pass all repaint only the
rects they are given, so a stale cursor in an un-repainted rect simply survives.

Three invariants now hold, and each has a test that fails without it:

1. **The damage bounds are derived from the sprite, never hand-written.**
   `ml_cursor_sprite_set()` rasterizes the sprite at five sub-pixel offsets and
   measures the exact set of pixels that differ from the background
   (`measure_ink()` in core/src/cursor.c), so `ml_cursor_damage()` /
   `ml_cursor_damage_at()` return a box that provably contains every pixel the
   pointer can touch — plus a margin for antialiasing. The old constant
   `(x-4, y-4, 24, 28)` happened to cover the default arrow and would silently
   stop covering it the moment the sprite changed.
2. **The pointer is composited exactly once per frame, over the union of its
   own damage clipped to what the frame repaints.** The cursor's stroke is
   translucent (`ml_rgba(20,22,30,200)`), so painting it twice over the same
   pixels blends it twice and darkens it — a "ghost cursor" that is a different
   bug with the same symptom. `present_region()` therefore paints the scene for
   every clip, then composites the pointer a single time.
3. **The compositor checks its own work.** `present()` tracks where the
   framebuffer currently shows the pointer and requires the frame's damage
   region to cover *both* that box and the box the pointer occupies now: miss
   the first and the previous cursor survives as a ghost, miss the second and
   the frame erases the pointer and draws nothing. Either way the rect is added
   and a warning is logged, so the guarantee holds even if a future caller
   forgets rule 1 — and the warning says which half was forgotten.

Screen edges are handled by clamping every damage rect to the screen: ink that
falls outside the framebuffer is never rasterized, so it never needs
restoring. Resizes (`ml_cursor_set_screen`) and warps refresh the same state.

Regression coverage, all wired into `scripts/run-tests.sh`:

- `out/test_cursor_damage` — the damage algebra: bounds vs a ground-truth
  rasterizer, both sides of a move, corners, off-screen clamping, resizes,
  custom sprites, region simplify completeness.
- `out/test_cursor_render` — a miniature compositor driven exactly like the
  real one, comparing the framebuffer byte for byte against a full repaint
  after every frame: slow moves, rapid bursts, edges/corners, overlapping UI,
  repeated redraws, scene changes, the KMS/fbdev scanout copy, and each of the
  three ways a caller can build a damage region that misses half the pointer's
  motion.
- `python3 scripts/test_cursor_trails.py` — the same comparison against the
  live framebuffer `mica-comp` actually presented. A plain `shot` re-renders
  the whole screen, so it *cannot* see this bug; only `--shot` can.

## Input, menus and hotkeys

Pointer/keyboard events route through the compositor to exactly one window per
event. Global hotkeys live in the compositor: Meta+Space toggles the launcher
(spawned on demand, killed on close), Meta+Left/Right switch workspaces.
Shell menus (panel, dock, desktop) are transient topmost windows whose input
handler lives in shellkit; every menu registers an owner slot so a self-close
never leaves a dangling pointer in the caller — and the compositor clears its
hover/drag/resize references whenever a window is freed (a use-after-free here
was caught by the scripted suite and fixed).
The compositor installs a SIGSEGV/SIGBUS/SIGABRT handler that prints a
backtrace before dying: a crashing compositor must leave evidence.

## Event-driven everything (spec §13)

The loop (core/src/event.c) is epoll + timerfd + signalfd. Nothing polls:
- clock repaints on a 1 s timer that disarms when the panel is hidden;
- dock magnification runs on pointer enter/move/leave inside its band only;
- animations are springs/easings ticked from a vsync timer that **disarms when
  the animation engine reports zero active animations** (tests/test_idle.c
  asserts the loop then produces ~0 wakeups and ~0 ms CPU);
- finder refreshes via inotify, search is per-keystroke capped prefix scan;
- status readers (battery/wifi) run on state-change events, not timers.

## Memory discipline (spec §12, §52)

Every cache has a byte budget and LRU eviction (core/src/cache.c): glyphs,
icons, finder thumbnails (8 MB cap). Notifications are RAM-only. No indexer,
no telemetry, no background updaters. tests/test_leak.c opens/closes 300
windows' worth of surfaces and asserts RSS returns to baseline.

## Hardware capability layer (v0.2)

    hardware/hwcap.{h,c}    result vocabulary, exit-code contract, codec table,
                            GPU capability DB, backend choosers, fixture roots
    hardware/hwprobe.{h,c}  PCI/DRM/CPU/mem/net/USB/storage/power/input probes,
                            the GL verification query, and the shared rules
    hardware/edid.{h,c}     EDID 1.3/1.4 parsing (DTD + established timings)
    hardware/backlight.{h,c} brightness backend with read-back verification
    hardware/audio.{h,c}    ALSA UAPI directly: cards, mixer, PCM, tone, WAV
    hardware/media.{h,c}    decoder + runtime decode probes, decode measurement
    hardware/kms.{h,c}      DRM/KMS and fbdev scanout, headless fallback
    hardware/drm_uapi.h     the ioctl/struct subset, so libdrm is not a dependency

Three rules live in this layer and nowhere else, because a rule implemented twice
*will* disagree with itself:

1. `hw_result` is five-state (`NOT_TESTED`, `UNSUPPORTED`, `FAIL`, `PARTIAL`,
   `PASS`) and `hw_report`/`hw_report_exit` map it onto exit codes
   (0/3/1/2/2). Tools add checks with evidence strings; the "not PASS" list is
   printed by the caller so a green run cannot hide an unverified row.
2. Cross-tool truths are single functions: `ml_audio_status_get()` decides the
   mixer verdict, `ml_net_link_status()` decides the link/address verdict,
   `bl_set_percent()` decides whether a brightness write was verified. All three
   were duplicated before and drifted; the fixture gates caught it.
3. `hw_using_fixture()` is honoured everywhere a claim would otherwise be about
   the host rather than the machine being described: address lookups, mixer
   ioctls, brightness writes, filesystem writes and TCP probes all refuse to
   report a result they cannot honestly have.

`tests/fixtures/make_sysfs.py` generates four `/sys`+`/proc` trees (iMac11,2,
iMac11,3, a VM, and a bare host); `tests/test_hardware.c` asserts what must be
detected and, just as importantly, what must be labelled NOT TESTED rather than
guessed. `scripts/run-tests.sh` runs the matrix plus four honesty gates on every
build.

## Performance modes (spec §34)

Beautiful / Balanced / Performance. First boot runs hwprobe + a blend
benchmark (hardware/hwprobe.c) and picks a mode; the user can override in
Settings. Modes scale shadow strength, animation duration and damage
coalescing — never correctness.
