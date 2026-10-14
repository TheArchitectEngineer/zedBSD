# T2: WS037 nvrtx R3（3 回目）・R4・R5（main 2d9d83e5e、CPU sequencer opcode 8 = CORE_RESUME）2026-10-14 夜

host awe@10.0.10.3、RTX 2070 を VFIO で QEMU の guest に（実機の証拠）。image は `yes | ZEDBSD_CONFIG=plan/ws037/tests/config-amd64-nvrtx.mk make firmware-licenses` の後、`plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v4/RN NVRTX_RUN=RN`（R3〜R7、各 約 2 分）。各 run の前に host を reboot、`prepare.sh --check` は 11 ok。起動は ~/nvrtx/start.sh v4RN。判定は QMP screendump の PNG。image は host の ~/nvrtx/v4R3〜v4R7（rm せず）。

## R3: PASS
`sequencer of 2000 bytes` → `sequencer: core resume` → `P2b ok: GSP-RM is up after 912 ms` → `P3 skipped (nvrtx.stop=P3)` → `P7 begin: unload`・`FWSEC-SB`・`booter_unload` → `P7 ok: region torn down`。login prompt あり。`resume …` の失敗の行なし。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw4/R3-a.png

## R4: PASS
`core resume`、`P2b ok … after 896 ms`、`P3 static: TU106-A vram 8192M uefi 0`、`P3 static: bar1 root 1f7653000 bar2 root 1f76c2000`、`P3 ok: client, device and subdevice`、`P4 skipped (nvrtx.stop=P4)`、`rpc 47: reply sequence 0, sent 4`（印のみ）、`P7 ok: region torn down`。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw4/R4-a.png

## R5: FAIL（`nvrtx.stop=P5`）
`P2b ok … after 911 ms`、`P3 ok: client, device and subdevice` の後:
```
nvrtx: P4 begin
nvrtx: P4 vram: region 1, block at 1f3f0000
nvrtx: P4 page tables at 1f3f00000
nvrtx: rm: control 00801813 status 00000056
nvrtx: P6 ok: channel and vaspace freed
nvrtx: P7 begin: unload
nvrtx: rpc 47: reply sequence 0, sent 7
nvrtx: P7 begin: FWSEC-SB / booter_unload
nvrtx: P7 ok: region torn down
```
`P4 ok: vaspace` が出ない（RM の control 0x00801813 が status 0x56 で失敗）。P7 は ok（後始末は動いた）。解析はしていない。R6・R7 は FAIL の所で止める手順により未実施（image は作って host に置いた）。
PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw4/R5-a.png（画面の上端は scroll して P1b 以前は切れている。R3・R4 の PNG に全部ある）

## 後始末
host は reboot 済み（QEMU 止まる）。ローカルに QEMU・build は残していない。補助 script: build/ws037-nvrtx/shot4.sh（repo 外）。所要 約 25 分。
