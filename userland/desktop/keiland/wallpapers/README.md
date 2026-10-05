# Keiland wallpapers (in the source tree)

2026-10-03 user: the compositor's built-in landscape and the blurred birch-and-lake picture are kept here and put in the disk image's `/usr/share/keiland/wallpapers/` (Settings > Wallpaper shows the name without `.ppm`).

| File | What | Origin |
| --- | --- | --- |
| `Lakeside.ppm` | 1920x1080, the landscape the compositor draws when it has no wallpaper file | rendered from `wallpaper_pixel()` and `ridge()` in `userland/desktop/wayland/glass.c` (same arithmetic, one pixel per output pixel) |
| `Birch-Lake.ppm` | 1920x1080, the blurred birch and lake used since the early Keiland tests | byte copy of `build/ws035-wallpaper/wallpaper-1080.ppm` (the `v2-soft-b` picture, see `plan/ws099/phase019/phase.md`) |
| `Lakeside.png` | the same picture as `Lakeside.ppm` | converted losslessly from `Lakeside.ppm` (same pixels; `userland/desktop/wallpapers/ppm-to-png.py`, rows filtered by the best of the five filters, zlib level 9, ws138-p001) |
| `Birch-Lake.png` | the same picture as `Birch-Lake.ppm` | converted losslessly from `Birch-Lake.ppm` (same pixels; `userland/desktop/wallpapers/ppm-to-png.py`, as above) |
