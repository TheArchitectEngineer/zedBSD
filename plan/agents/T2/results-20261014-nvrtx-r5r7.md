# T2: WS037 nvrtx R4・R5・R6（R7 未実施）main 8007f35a5（RM alloc header 32 byte）2026-10-14 夜

host awe@10.0.10.3、RTX 2070 を VFIO で QEMU の guest に（実機の証拠）。image は `yes | ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk make firmware-licenses` の後、`plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v5/RN NVRTX_RUN=RN`（R4 52 s、R5 96 s、R6・R7 rc 0、warning は jobserver の注記のみ）。host の ~/nvrtx/v5R4〜v5R7 に置いた（rm せず）。各 run の前に host を reboot、`prepare.sh --check` 全 ok。起動は ~/nvrtx/start.sh v5RN。判定は QMP screendump の PNG。

## R4: PASS
`P2b ok … after 920 ms`、`P3 ok: client, device and subdevice`、`P4 skipped (nvrtx.stop=P4)`、`P7 ok: region torn down`。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw5/R4-a.png

## R5: PASS
`P3 ok` → `P4 vram: region 1, block at 1f3f00000` → `P4 page tables at 1f3f00000` → **`P4 ok: vaspace`** → `P5 skipped (nvrtx.stop=P5)` → `P6 ok: channel and vaspace freed` → `P7 ok: region torn down`。0x56 は消えた。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw5/R5-a.png

## R6: FAIL（P5 は全部 ok、P7 が fail）
```
nvrtx: P4 ok: vaspace
nvrtx: P5 begin
nvrtx: P5 copy engine b runlist 8 (graphics 0), method buffer 8192
nvrtx: P5 ok: channel 8 on runlist 8
nvrtx: P5 ok: fence 0x1234 reached after 516 us
nvrtx: P5 ok: fence 0x1235 reached after 1001 us
nvrtx: P5 ok: copy engine filled 64 words
nvrtx: P8 is not in this kernel; stopping after P5
nvrtx: P6 skipped (nvrtx.stop=P6); the unload frees the objects
nvrtx: P7 begin: unload
nvrtx: P7 unload call: 42
nvrtx: P7 fail: the GSP did not stop (42); memory kept, power cycle needed
```
合格の印のうち P5 の 4 つは全部出た。`P7 ok: region torn down` が出ず `P7 fail`（unload の call が 42、GSP が止まらない）。20 秒後に撮り直しても同じ画面（R6-b）。pmemsave は P5 の fence は来たので取っていない。解析はしていない。手順どおり R7 は流していない（image は v5R7 に作って host に置いた）。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw5/R6-a.png、R6-b.png（同じディレクトリ。画面の上端は scroll して P1b 以前は切れている）

## 後始末
QEMU は quit、host は reboot 済み。ローカルに QEMU・build は残していない。補助 script: build/ws037-nvrtx/shot5.sh・cycle.sh（repo 外）。所要 約 20 分。
