# menu-bar/

Panel role of ../desktop/mica-shell.c (`--role panel`): app menus
(System/File/Edit/View/Go/Window/Help), status cluster (wifi, volume, battery,
clock), control center popover. Clock repaints are damage-limited to the clock
rectangle (measured: 6.5 kpx vs 37 kpx full strip, docs/performance.md).
