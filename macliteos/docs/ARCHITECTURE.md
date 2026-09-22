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

## Performance modes (spec §34)

Beautiful / Balanced / Performance. First boot runs hwprobe + a blend
benchmark (hardware/hwprobe.c) and picks a mode; the user can override in
Settings. Modes scale shadow strength, animation duration and damage
coalescing — never correctness.
