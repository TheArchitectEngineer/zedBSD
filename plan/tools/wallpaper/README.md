# Wallpaper tools

Tests of the desktop's wallpaper reading (PNG and JPEG, `userland/desktop/picture/wallpaper.c`), kept from
[WS138](../../ws138/ws.md) (background pictures from PPM to PNG, completed 2026-10-05).

| File | Where it runs | What it checks |
| --- | --- | --- |
| `run-host-wallpaper-decode.sh` + `host-wallpaper-decode.c` | host, ASan/UBSan, about 30 s | The shared decoder built with libz-, libpng- and libjpeg-compat from their sources: the tree's `Lakeside.png` and `Birch-Lake.png` decode to the pixels Python's own reader gives; transparency is composited over black; a JPEG decodes within a small mean difference, a grey JPEG stays grey, a CMYK JPEG is refused; a side over 8192 is EFBIG; a text file, a cut PNG and a PPM are refused. Needs python3 and ImageMagick's `convert`. |
| `wallpaper-time.sh` | QEMU guest of the Settings image (`plan/ws089/tests/settings-guest.sh start`) | The compositor reads a PNG and a JPEG wallpaper at start and during the session (glass.c's thread), refuses a PPM (EINVAL, errno 3 on zedBSD), and prints the medians of `ZWL STARTUP step=wallpaper` (recorded, not judged). |
| `greeter-wallpaper.sh` | QEMU guest of the criteria image (`plan/ws035/tests/zdesktop-guest.sh start`, `login=graphical`) | Empties the autologin file, starts `sessiond --graphical`, checks the greeter's log for `ZWL STARTUP step=wallpaper` without `ZWL GLASS no wallpaper`, takes `greeter.png` (to look at), and puts the autologin file back. |

The guest scripts take `GUEST_RUNTIME` from the caller (default as for their guest helper) and an optional output directory
(default `build/wallpaper/...`).
