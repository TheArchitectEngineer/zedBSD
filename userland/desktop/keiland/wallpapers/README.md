# Keiland wallpapers (in the source tree)

2026-10-03 user: the compositor's built-in landscape and the blurred birch-and-lake picture are kept here and put in the disk image's `/usr/share/keiland/wallpapers/` (Settings > Wallpaper shows the name without `.png`).  Since ws138-p002 the pictures are PNG; the desktop reads PNG and JPEG only.

| File | What | Origin |
| --- | --- | --- |
| `Lakeside.png` | 1920x1080, the landscape the compositor draws when it has no wallpaper file | rendered from `wallpaper_pixel()` and `ridge()` in `userland/desktop/wayland/glass.c` (same arithmetic, one pixel per output pixel) as `Lakeside.ppm`, then converted losslessly (same pixels; `userland/desktop/wallpapers/ppm-to-png.py`, rows filtered by the best of the five filters, zlib level 9, ws138-p001); the PPM is in git's history |
| `Birch-Lake.png` | 1920x1080, the blurred birch and lake used since the early Keiland tests; the default wallpaper | byte copy of `build/ws035-wallpaper/wallpaper-1080.ppm` (the `v2-soft-b` picture, see `plan/ws099/phase019/phase.md`) as `Birch-Lake.ppm`, then converted losslessly as above; the PPM is in git's history |
