# T2: WS037 nvrtx 方式 B 再試験（R2・R3・R4、main 2a8f5054a = P7・P2b の直しを含む）2026-10-14 夜

QEMU の guest に渡した実機の RTX 2070（TU106）の証拠。host awe@10.0.10.3。image は `plan/tools/guest/test-image.sh --no-harness plan/ws037/tests/config-amd64-nvrtx.mk build/ws037-nvrtx/v2/<R> NVRTX_RUN=<R>`（前に `make firmware-licenses`）。
各 run の前に host を reboot、`prepare.sh --check` 全 ok。起動は ~/nvrtx/start.sh（run 名 v2R2・v2R3）。判定は QMP の screendump の PNG（serial/console log は不使用）。

| run | 判定 | 要点 | PNG（/home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw2/） |
| --- | --- | --- | --- |
| R2 | PASS | `P1a ok after 0 ms`、`P1b ok: region 01ffe000-01ffee00`、`P2a skipped (nvrtx.stop=P2a)`、`P7 left: region 01ffe000-01ffee00 without a started GSP; reboot the host`（行は 80 桁で切れて `before the next run` は画面に出ない）、login prompt まで。P7 fail 無し | R2-a.png（boot 画面に全行）、R2-dmesg.png |
| R3 | FAIL（P2b） | P0・P1・`P2a begin: booter_load`、`P2a memory: libos 7fe00000 rmargs 7fe31000 queues 7f800000+81000`、`P2a memory: logs 7fe01000 7fe11000 7fe21000, 10000 bytes each`、`P2a ok: RISC-V running`、`P2b begin: waiting for init done`、200/1000/3000/10000 ms の各 2 行（下）、**`P2b fail (42): memory kept for a dump; reboot the host`**（10 s で出た。`P2b ok` 無し）、login prompt 到達 | R3-a.png（全部の行）、R3-tail.png、R3-mbox.png |
| R4 | 未実施 | R3 が FAIL（手順どおり） | - |

R3 の P2b の行（全 t で同じ）: `cmd put 2 got 0; msg size 0 put 0 got 0`、`mbox 000000ca 00000000 riscv 00000035 boot 11000000; logs …`。
つまり GSP は cmdq を 1 つも読まず（got 0）、msgq の header も書かず（size 0）、MAILBOX0 = 0xca、RISC-V の状態 0x35、secure scratch 14 = 0x11000000。
**`logs A B C` の put の値は kmsg の行が約 80 文字で切れていて dmesg にも画面にも出ない**（`dmesg | grep mbox | cut -c60-` でも `logs` で終わる）。値は P1 が行を短くしないと読めない。

## pmemsave（R3 FAIL の guest を止める前、QMP）
host ~/nvrtx/dump3/ と centris の /home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/dump3/ に同じ物:
log0.bin（7fe01000、64 KiB）、log1.bin（7fe11000）、log2.bin（7fe21000）、queues.bin（7f800000、0x81000 = 528384 byte）、libos.bin（7fe00000、4 KiB）、rmargs.bin（7fe31000、4 KiB、追加）。
非 0 の byte 数: log0 86、log1 47、log2 47（`strings` で `LOGINIT`・`LOGRM` の語の逆さ文字程度の header だけ）、queues 521、libos 49、rmargs 7。解析はしていない。

## 後始末
R3 の QEMU は quit、host は `systemctl reboot` 済み（WPR2 は消える見込み、次の run の前に prepare.sh --check）。image は host の ~/nvrtx/v2R2・v2R3・v2R4 に置いたまま（rm せず）。
補助（repo 外・commit 無し）: build/ws037-nvrtx/{shot.sh,type.sh,hosttype.py,hostdump.py}。所要 約 35 分（image 3 つの build 約 10 分）。
