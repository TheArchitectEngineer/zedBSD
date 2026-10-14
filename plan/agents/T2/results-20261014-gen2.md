# T2 results (2026-10-14, new generation)

## T2-616 (= ledger T1-616): BUG-306 retest, ws217-p004 rotation redraw delay, main daa3a118d (contains 02c02dbf4). QEMU Venus (KVM), AAT image. Verdict: PASS on (1)(2)(3)(5)(6); FAIL on (4) `ms=` <= 2000 (6 of 6 size-changing turns were 2100..2283).

Image: `AAT_CONFIG=plan/ws217/tests/config-amd64.mk plan/tools/aat/build-image.sh build/t2-616/img` in /home/awe/zedBSD-worktrees/t2 (tree = main daa3a118d, own packages/toolchain build, 21 m 09 s). Guest: `GUEST_RUNTIME=build/t2-616/run plan/ws035/tests/zdesktop-guest.sh start` (renderer = main's build/ws035-sq-venus, read-only symlink). Auto-login session as kei; `settings display` started via `su kei` with XDG_RUNTIME_DIR/WAYLAND_DISPLAY (no welcome dialog). T1 was running a gdbstub QEMU on the same host at the same time (CPU shared; may lengthen ms=).
Steps: r1 0>90, r2 90>0, r3 0>90, r4 90>270, r5 270>0, r6 0>90, r7 90>0 (r6/r7 are an extra pair: the r1 1/2/3 s PNGs are invalid, see below). 30 s between presses; clicks by `aat click` at the PNG's position (keiland-shot is the physical output, so a rotated PNG is read in physical coordinates).
Artifacts (abs): /home/awe/zedBSD-worktrees/t2/build/t2-616/ : session.log (copied before any logout; no logout done), key-lines.txt, slow-pass.txt, displays-after-r3.txt, displays-final.txt, press.sh, png/r<n>-{1s,2s,3s,late}.png (+ .err for the failed ones).

| turn | change | ms= | times (prepare/swap/caches, told_at_ms) | ZSETTINGS RESIZE (swapchain_ms/shown_ms/at_ms) | drawn (WxH, was, after_ms) | shown | saved |
| r1 | 0>90 | 2283 | 829/1411/889, 132254 | 776x800: 1606/1809/132254 | 776x800 was 1180x680, 852 | held_ms=946 late=0 | writer errno=0 |
| r2 | 90>0 | 2178 | 624/1512/911, 165668 | 1180x680: 1798/1998/165669 | 1180x680 was 776x800, 893 | held_ms=986 late=0 | writer errno=0 |
| r3 | 0>90 | 2100 | 681/1376/883, 211953 | 776x800: 1478/1676/211952 | 776x800 was 1180x680, 897 | held_ms=988 late=0 | writer errno=0 |
| r4 | 90>270 | 70 (same size, no hold) | - | - | - | - | writer errno=0 |
| r5 | 270>0 | 2158 | 578/1536/895, 293543 | 1180x680: 1737/1849/293543 | 1180x680 was 776x800, 834 | held_ms=933 late=0 | writer errno=0 |
| r6 | 0>90 | 2158 | 689/1428/897, 335145 | 776x800: 1631/1830/335146 | 776x800 was 1180x680, 908 | held_ms=1006 late=0 | writer errno=0 |
| r7 | 90>0 | 2192 | 623/1527/903, 368871 | 1180x680: 1628/1853/368870 | 1180x680 was 776x800, 895 | held_ms=993 late=0 | writer errno=0 |

(`KWL OUTPUT rotate hold windows=1 bounds=776x1160` / `1256x680`, `KWL CONFIGURE client=3 serial=10/15/20/28/33/38`, `KWL BOUNDS changed` and `KWL IMPORT client=3` 24 lines, `KWL OUTPUT window` lines are all in key-lines.txt, 110 lines; `KWL DISPLAYS saved by=writer errno=0` after every turn, ms=4787..14493 on the writer thread.)

Criteria:
1. late=0 on all 6 size-changing turns: PASS. held_ms 933..1006; **r6 held_ms=1006 (>= 1000, `late=0`)**: the criterion text says held_ms < 1000; this one value is 6 ms over; reported, judge by Q1. The others are < 1000.
2. drawn WxH differs from was= on all 6: PASS (0>90 776x800 was 1180x680; 90>0 1180x680 was 776x800). after_ms 834..908.
3. PNGs: keiland-shot 1 s failed (`KEILAND-SHOT error no-active-compositor`, the turn holds the compositor) in r2, r3, r5, r6, r7, ok in r4; shots started at ~1.0 s of each press and the next at ~2.2 s (a shot takes 1.0..3.8 s), so the "2s" PNG is taken at 2.0..2.5 s and the "3s" at 3.0..5.3 s after the click (start times in each run's output, see press.sh; not stored per file except in this note: r2 1.01/2.20/4.54, r3 1.01/2.25/5.20, r4 1.01/2.01/3.01, r5 1.01/2.56/4.96, r6 1.01/2.16/5.30, r7 1.01/2.19/4.46 s). No clipped window or old 1180x680 picture drawn at x=12 in any valid PNG (r2-2s/3s, r3, r4, r5, r6, r7: window at the new bounds and size; r2-2s and r6-2s show the new-size window with the previous page state (rotation card and the "was" resolution text) for a moment, repainted by 3 s). **r1-1s/2s/3s.png are invalid** (taken by VNC, which did not follow the turn; they show the pre-turn picture); r1-late.png is a valid keiland-shot; use r6 as the 0>90 series.
4. ms= <= 2000: **FAIL** on all 6 size-changing turns: 2283, 2178, 2100, 2158, 2158, 2192 (r4 70). Same CPU box as T1's concurrent QEMU; swap_ms 1376..1536 is the bulk (T1-614 had 1133..1244).
5. Settings stays mapped (client 3, surface 19) through all turns (`aat windows`); `PRESENT_FAILED`/`ZSETTINGS FAILED`/`KWL FAILED` lines: 0: PASS.
6. displays.conf (kei's ~/.config/keiland/displays.conf) after r3 (90): `rotate=Venus virtual display 0 90`; final after r7 (0): no rotate line (`version=1`, `mode=extended`); `KWL DISPLAYS saved by=writer errno=0` after each turn: PASS.

SLOW_PASS us > 1000000 (slow-pass.txt): 1356000 at_ms=74298 (schedule_us, startup), 3681000 at_ms=77980 (schedule_us, startup), and six turn passes with clients_us 2.10..2.29 s at at_ms 133669, 167182, 213331, 295082, 336576, 370401 (the Settings set_rotation request; main-thread work, same as T1-614).
No ZSETTINGS SLOW-FRAME line above the threshold was extracted separately (included in key-lines.txt if present).
Time: image 21 m 09 s, guest session ~6 min. QEMU stopped.
