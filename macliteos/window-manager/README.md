# window-manager/

The WM is implemented inside the compositor process (../compositor/comp.c):
z-order list, focus policy, workspace switch with slide animation, drag/resize
with live damage, minimize-to-dock handoff via MC_WIN_ACTION. Keeping WM and
compositor in one process is deliberate: focus changes and restacks become
single-threaded state edits followed by one damage region — no IPC round trip
per keystroke (spec §3: smoothness first).
