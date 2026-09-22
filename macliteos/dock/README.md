# dock/

Dock role of ../desktop/mica-shell.c (`--role dock`). Event-driven gaussian
magnification (formula shared with the design mockup in the repo root),
autohide, minimize target. Wakes only on pointer enter/leave/move inside its
band and on window events; idle CPU is 0 (see tests/test_idle.c).
