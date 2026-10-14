# T1-641: WS037 R7c（最初の compute の shader）2026-10-15

host awe@10.0.10.3（RTX 2070 を VFIO で QEMU guest に。実機の GPU の証拠）。main 227e9173a。image は `yes | make firmware-licenses ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk` の後 `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v7c NVRTX_RUN=R7c`（rc 0、約 7 分、warning 0）。host の ~/nvrtx/v7c に置き、~/nvrtx/start.sh v7c（rombar=0）。run の前は host を reboot 直後（`prepare.sh --check` は ok 以外 0 行）。判定は QMP screendump。

## 結果: PASS
`P9c begin` → arena at 1e9800000、own buffers f5000 → page tables → `P9c ok: vaspace` → `P9c ok: compute channel 3 on runlist 0` → `P9c program: 16 instructions, 10 registers` → `P9c ok: fence 0x4242 reached after 640 us` → **`P9c ok: compute wrote 32 words of 42`** → `P8 ok: ... lit 0`（rombar=0）→ `P8b is not in this kernel` → `P6 ok: channel and vaspace freed`（compute frees failed 無し）→ `P7 ok: region torn down`。rm: の行・channel error 無し。info registers HLT=1（固まらず）。login: は scroll で切れて画面に無い（前回の R7 と同じ。login の確認は未実施）。
PNG: /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/v7c-b.png、v7c-final.png
後始末: QEMU 停止、host reboot 済み。ローカルの QEMU は起動していない。

# R8b: 未実施（止めた）
main（81ae41ad8）に R8b が無い。`plan/ws037/tests/config-amd64-nvrtx.mk` に NVRTX_RUN=R8b が無く、`nvrtx.disp`・`P8b ok` が src にも無く、phase006 に「T への手順（R8b」の節も無い。P1 の b893faeb6〜eb3ebc404 は branch agent/p1-ws037b にだけあり、main に merge されていない。merge は Q1。main に入れたら再依頼を。

# T1-642: R8b（RTX の display の引き継ぎと flip、rombar=1）2026-10-15 結果: FAIL
main 20abf069a、image `NVRTX_RUN=R8b`（build/ws037-nvrtx/v8b、host ~/nvrtx/v8b）、`~/nvrtx/start-rombar1.sh v8b`。host は reboot 後 5 分、prepare.sh --check ok のみ。ユーザーが monitor の前（起動の指示は Q1 経由）。
screendump（10 秒おきに 14 枚、起動から約 10〜141 s）は 2 種類だけ（最初の 5 枚と 6 枚目以降が同じ）。起動から約 10 s の時点で既に下の最終の画面で、`surface A shown`・`flip` は一度も出ていない（時刻なし）。
nvrtx の行（全行の印）:
- `P8 ok: display, heads f, windows ff, lit 1`（head 0 SOR 1 TMDS-A、1280x1024 of 1688x1066、clock 108000000）
- `P8b begin` → `P8b head 0 armed: view 03200500 raster 042a0698 clock 066ff300, SOR 1 00` → `P8b surfaces 1280x800 pitch 5120 at 1e8f00000 and 1e9300000` → `P8b channel c57d.0 made`
- **`P8b fail: core owners notifier 00000000 after 2 s, get 18c put 18c`**
- `P6 ok: channel and vaspace freed` → `P7 begin: unload` → `P7 unload call: 42` → **`P7 fail: the GSP did not stop (42); memory kept, power cycle needed`**
`rm:` の行は画面に出ていない。guest は HLT=1 で固まりはしない。P8 の raster/viewport は 1280x1024 に対し surface は 1280x800（view 0320 0500）。解析はしない。
PNG: /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/v8b-6.png、v8b-14.png（同じ画面）。
後始末: QEMU 停止、host reboot（P7 fail のため power cycle 相当）。ローカルの QEMU は起動していない。
