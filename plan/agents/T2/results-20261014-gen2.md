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

## T1-617 (BUG-309 pc98 partition on cylinder boundary) — PASS (QEMU pc98 only; real machine 未実施)

- main 82b726bfd をマージした worktree。image build: `make ZEDBSD_CONFIG=config/ci/config-pc98.mk disk-image`（build/i386/sysroot は main の共有 sysroot への symlink）、1m44s。
- image: /home/awe/zedBSD-worktrees/t2/build/pc98/hdd-image.img（残す。UAT 用）
  sha256 6fa99b5279955534215785f926ec2c998ae6b4ef7e76bd67d0fa607214c33393 、185708544 bytes
- sector 1 の BOOT の entry（od -j 512）: `a1 91 00 00 00 00 10 00 00 00 10 00 10 07 6a 0a 42 4f 4f 54 ...` → 期待の `a1 91 00 00 00 00 10 00 00 00 10 00 10 07` で始まる（一致）。
- sector 0（IPL）: `eb 0a 90 90 49 50 4c 31 00 00 00 1e fa fc 8c c8 ...`
- `python3 plan/tools/pc98-boot.py /home/awe/qemu-pc98/build/qemu-system-i386 build/pc98/hdd-image.img /home/awe/zedBSD-worktrees/t2/build/t2-617` exit 0、39.7s。
- PNG: /home/awe/zedBSD-worktrees/t2/build/t2-617/login.png （"login:" 表示）、/home/awe/zedBSD-worktrees/t2/build/t2-617/command.png （root login、`uname -a; echo WSDONE` が i386 と WSDONE を表示）

## T1-615 の試験 C・D・E（T2、2026-10-14、BUG-307 の bec395969 を含む main a5d83fa78）

image: `sh plan/tools/guest/build-ssh-image.sh build/bug307-after`（/home/awe/zedBSD-worktrees/t2/build/bug307-after、49 秒）。作業は /home/awe/zedBSD-worktrees/t2/build/t2-307/（volume、入力 in/、各回の *.gdb *.cmd snap-*.img chk-*.img）。GUEST_RUNTIME=build/t2-run。QEMU は host 全体で T1 と並走（port は別）。driver は /tmp/claude-1000/t2s/case.sh（試験の道具、source でない）。
QEMU の証拠のみ。実機は未実施。

道具の事実（依頼の手順からの変更）
- `hbreak ufs_file_sync` は prologue の後に止まり `$rsp` が戻り番地にならない（1 回目の D が空振り）。`hbreak *ufs_file_sync` に変えた。
- guest で試験の command を打つ SSH の login 自体が fsync を起こしうるので、command は gdb を付ける前に開いた SSH の中で `read go` で待たせ、gdb の `continue` の後に fifo で起動した。
- gdb の stop packet が `warning: Invalid remote reply: fb7e3e80...` で壊れて continue が戻らない回があった（C3・C8・C11。負荷つき、-smp 4 と 1 の両方）。N 回目まで届かない回もあった（C5・C6・C10）。guest の起動直後の最初の SSH/scp が "Connection closed" になる回もあった（C5・C7）。これらは試験の結果でなく道具の不調として扱う。

結果
- D（消した file の block の再使用）: **FAIL（1 回目）/ PASS（retry）**。
  - D 1 回目: snap-D.img の `/jour/old` 無し、`/jour/new` は 8388608 byte だが `cksum` 3451288497（host の new は 741758398、old は 624633356）、先頭 36 block (288 KiB) は一致、block 36 以降が不一致（`cmp -l` 8058173 byte）。`UFS OK`。gdb は Breakpoint 1 → file_fsync の戻り（build/t2-307/D.gdb）。
  - D 2 回目（変更なし）: `cksum` 741758398 で一致、old 無し、`UFS OK`（D2.gdb）。
  - 2 回で結果が違う。最初の fsync が同じ `sync /jour/new` の戻りだったか（途中の fsync で止まった可能性）は、この道具では区別できない。原因の解析はしていない。
- E（rename の後の directory の fsync）: **PASS**。N=2、写しで `/jour/e/final` 3000 byte `cksum` 715748844（host の e1 と一致）、`/jour/e/tmp` 無し、`UFS OK`（E.gdb・chk-E.img）。
- C（揃っていない volume、sector 63）: **PASS（ただし stop が 16 個目の file でなく 15 個目の後）**。C9（-smp 1、N=16）: f1〜f15 の `cksum` が host の c1〜c15 と全て一致、f16 無し、d1〜d15 あり（30 names）、切り出した物（chk-C9-part.img）が `UFS OK`。別回 C2（-smp 4、N=16）も f1〜f14 一致・f15/f16 無し・`UFS OK`。つまり N=16 の stop は他に 1〜2 回の fsync を数えて f16 の前に落ち、f16 の fsync の戻りでの確認にはなっていない。
- C（揃った volume `/dev/nvme1n1`、同じ試験を 1 回）: **未実施**。C4・C6・C7・C10・C11 は gdb の不調か N に届かず（上）、有効な写しが取れなかった。
- 既知の限界: 負荷つき（dd 並走）の gdbstub は不安定で、N=16 を厳密に合わせられない。
