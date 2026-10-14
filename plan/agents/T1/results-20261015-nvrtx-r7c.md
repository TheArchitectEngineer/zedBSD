# T1-641: WS037 R7c（最初の compute の shader）2026-10-15

host awe@10.0.10.3（RTX 2070 を VFIO で QEMU guest に。実機の GPU の証拠）。main 227e9173a。image は `yes | make firmware-licenses ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk` の後 `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v7c NVRTX_RUN=R7c`（rc 0、約 7 分、warning 0）。host の ~/nvrtx/v7c に置き、~/nvrtx/start.sh v7c（rombar=0）。run の前は host を reboot 直後（`prepare.sh --check` は ok 以外 0 行）。判定は QMP screendump。

## 結果: PASS
`P9c begin` → arena at 1e9800000、own buffers f5000 → page tables → `P9c ok: vaspace` → `P9c ok: compute channel 3 on runlist 0` → `P9c program: 16 instructions, 10 registers` → `P9c ok: fence 0x4242 reached after 640 us` → **`P9c ok: compute wrote 32 words of 42`** → `P8 ok: ... lit 0`（rombar=0）→ `P8b is not in this kernel` → `P6 ok: channel and vaspace freed`（compute frees failed 無し）→ `P7 ok: region torn down`。rm: の行・channel error 無し。info registers HLT=1（固まらず）。login: は scroll で切れて画面に無い（前回の R7 と同じ。login の確認は未実施）。
PNG: /home/awe/zedBSD-worktrees/t1/build/ws037-nvrtx/hw6/v7c-b.png、v7c-final.png
後始末: QEMU 停止、host reboot 済み。ローカルの QEMU は起動していない。

# R8b: 未実施（止めた）
main（81ae41ad8）に R8b が無い。`plan/ws037/tests/config-amd64-nvrtx.mk` に NVRTX_RUN=R8b が無く、`nvrtx.disp`・`P8b ok` が src にも無く、phase006 に「T への手順（R8b」の節も無い。P1 の b893faeb6〜eb3ebc404 は branch agent/p1-ws037b にだけあり、main に merge されていない。merge は Q1。main に入れたら再依頼を。
