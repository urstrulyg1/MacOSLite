# launcher/

Meta+Space overlay: role `launcher` of ../desktop/mica-shell.c, started ON
DEMAND by the compositor when the key fires and exited when it closes — it
costs nothing while you don't use it (spec §37). Search is a capped prefix
scan over the app registry + $PATH, performed per keystroke, never indexed.
