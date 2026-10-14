# T2: WS037 nvrtx 方式 B R3 再試験（main 714ab8477 = RMARGS 0x1000 の直し）2026-10-14 夜

host awe@10.0.10.3、RTX 2070（TU106）を VFIO で QEMU の guest に。image は `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v3/R3 NVRTX_RUN=R3`（`yes | make firmware-licenses` は worktree に config.mk が無く失敗したが、image には firmware が入り P0 が読めた）。host は前の run の後に reboot 済みで、`prepare.sh --check` は全 ok。起動は ~/nvrtx/start.sh v3R3。判定は QMP の screendump の PNG（serial/console log は不使用）。

## R3: FAIL（P2b、前回より進んだ）

画面の nvrtx の全行（PNG は R3-a.png に全部が収まる）:
```
nvrtx: P0 vbios 328192 bytes, fwsec v2 39440 bytes
nvrtx: P0 falcons: SEC2 reset bit 14
nvrtx: P0 ok vram 8192M wpr2 1f7800000+135M heap 105M
nvrtx: P1a begin: devinit
nvrtx: P1a ok after 0 ms
nvrtx: P1b begin: FWSEC-FRTS at 1ffe00000
nvrtx: P1b ok: region 01ffe000-01ffee00
nvrtx: P2a begin: booter_load
nvrtx: P2a memory: libos 7fe00000 rmargs 7fe31000 queues 7f800000+81000
nvrtx: P2a memory: logs 7fe01000 7fe11000 7fe21000, 10000 bytes each
nvrtx: P2a ok: RISC-V running
nvrtx: P2b begin: waiting for init done
nvrtx: P2b 200 ms: cmd put 2 got 2; msg size 40000 put 0 got 0
nvrtx: P2b 200 ms: mbox 7fe00000 00000000 riscv 00000035 boot 11000000
nvrtx: P2b 200 ms: log put 4a 0 c
nvrtx: rpc: message 1020 (1212 bytes) while waiting for 1001
nvrtx: rpc: sequencer of 2000 bytes
nvrtx: sequencer: opcode 8 not supported
nvrtx: P2b 727 ms: cmd put 2 got 2; msg size 40000 put 2 got 2
nvrtx: P2b 727 ms: mbox 00000000 00000000 riscv 00000000 boot 13300000
nvrtx: P2b 727 ms: log put a4 0 78
nvrtx: P2b fail (21): memory kept for a dump; reboot the host
```
- 200 ms の行は合格の印（`cmd put 2 got 2; msg size 40000`）を満たす。RMARGS の直しが効き、GSP は cmdq を読み、msgq を作った（前回は got 0・size 0）。
- 727 ms で GSP から message 0x1020（1212 byte）が来て、続く CPU sequencer（2000 byte）の中の **opcode 8 が未対応**で P2b が失敗（err 21）。`P2b ok` は出ず、`P7` は動かず（「memory kept for a dump」）。
- login prompt まで到達（PNG に `login:`）。起動の他の行は正常。R4 は R3 が FAIL のため未実施（手順どおり。image `build/ws037-nvrtx/v3/R4` は build 中・未使用）。

## 取った物
- PNG: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw3/R3-a.png、R3-b.png（FAIL 後に dump 直前、同じ画面）
- pmemsave の 6 つ（FAIL の guest を止める前）: /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/dump4/{log0,log1,log2,queues,libos,rmargs}.bin（host の ~/nvrtx/dump4/ にも同じ物）。非 0 の byte 数 log0 575、log1 47、log2 409。解析はしていない。
- 所要 約 25 分（build 約 10 分）。

## 後始末
QEMU は quit、host は `systemctl reboot` 済み。image は host の ~/nvrtx/v3R3 に残した（rm せず）。補助 script は build/ws037-nvrtx/shot3.sh（repo 外）。
