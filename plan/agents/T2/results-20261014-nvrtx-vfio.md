# T2: WS037 nvrtx 方式 B（VFIO、RTX 2070 TU106）R0〜R4（2026-10-14）

実機の GPU を QEMU の guest に渡した証拠（host awe@10.0.10.3、QEMU 10.0.13、KVM）。R0 PASS、R1 PASS、R2 FAIL（P7）で止めた。R3・R4 は未実施。

## command line の差
phase004 の 3.2 節のとおり。ただし image は `if=virtio` では zedBSD が disk を見つけず（`boot-storage wait expired`、`devices=0`、PNG build/ws037-nvrtx/hw/R0-1.png）、
`-drive if=none,id=boot,file=…,format=raw -device nvme,serial=zedbsd-boot,drive=boot,bootindex=1 -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0,port=2`（boot-test.sh と同じ）に変えた。
4 GiB 超の BAR は X-PciMmio64Mb 無しで割り当たった（bar1 7000000000、bar3 7010000000）。起動は host の ~/nvrtx/start.sh（/tmp/t2-start.sh の写し、repo 外）。
判定の道具: image は CONFIG_PCAT_SERIAL_MIRROR=n のため serial.py の login は不可（`never saw 'login: '`）。QMP の `sendkey` で仮想の VGA の console に root/root で login し、`dmesg | grep nvrtx` を打って screendump（QMP）で読んだ。serial/console log は判定に使っていない。

## 結果
| run | 判定 | nvrtx の行（要点） | PNG（/home/awe/zedBSD-worktrees/t2/build/ws037-nvrtx/hw/） |
| --- | --- | --- | --- |
| R0 | PASS | `N0 ok chip=166 TU106 rev a1 (Turing)`、`N1 gop 640x480 at 80000000 is not on this GPU`、`N1 done; TU106 left to the firmware`、login まで | R0-2.png（login）、R0-3.png（dmesg） |
| R1 | PASS | `P0 files: booters 59272/39304 bytes…`、`P0 image: 28528288 bytes in 28 chunks`、`P0 vbios 328192 bytes, fwsec v2 39440 bytes`、`P0 falcons: SEC2 reset bit 14`、`P0 ok vram 8192M wpr2 1f7800000+135M heap 105M`、`P1a skipped (nvrtx.stop=P1a)`、login まで | R1-boot.png、R1-dmesg.png |
| R2 | FAIL | `P1a ok after 0 ms`、`P1b begin: FWSEC-FRTS at 1ffe00000`、`P1b ok: region 01ffe000-01ffee00`、`P2a skipped (nvrtx.stop=P2a)`、`P7 begin: FWSEC-SB`、`P7 booter_unload`、`booter: mailbox 00000029 000000ff`、**`P7 fail: region still up to 01ffee00; power cycle needed`**（P7 ok が出ない） | R2-boot.png、R2-dmesg.png |
| R3 | 未実施 | R2 の FAIL で止めた | - |
| R4 | 未実施 | 同上 | - |

- R2: P1b は ok。stop=P2a の後、guest の boot の最後に P7 が走り、booter_unload の mailbox が `00000029 000000ff` で、WPR2 の上限が 0 に戻らず FAIL。`P1b stop: region … already set` は出ていないので host の reboot と再試行はしていない。固まりは無し（login prompt まで到達）。解析はしていない。
- 後の状態: R2 の QEMU は止めた。host の GPU は WPR2 が残った状態（`power cycle needed`）。次の run の前に host の再起動が要る（未実施）。
- host の ~/nvrtx/R0〜R4/hdd-image.img（約 2.2 GB ずつ）は置いたまま。
- 所要: image の scp 約 1 分、run 3 回で約 12 分。

## 追記: host 再起動後の R3（2026-10-14 夜、Q1 の続行指示）
host を `sudo systemctl reboot` → 戻って `prepare.sh --check` が全部 ok（11 行、TU106 の 4 function が vfio-pci）。同じ command line で R3（`nvrtx.gsp=1 nvrtx.stop=P3`）。

| run | 判定 | nvrtx の行 | PNG |
| --- | --- | --- | --- |
| R3 | FAIL（P2b） | P0 は R1 と同じ（`P0 ok vram 8192M wpr2 1f7800000+135M heap 105M`）、`P1a begin: devinit`、`P1a ok after 0 ms`、`P1b begin: FWSEC-FRTS at 1ffe00000`、`P1b ok: region 01ffe000-01ffee00`（warm reboot 後は `already set` が出ず、WPR2 は消えていた）、`P2a begin: booter_load`、`P2a ok: RISC-V running`、`P2b begin: waiting for init done`。**`P2b ok: GSP-RM is up` が出ない**（起動から約 5 分以上経っても `P2b ok`・`P2b fail` のどちらも出ない） | R3-boot.png、R3-dmesg.png、R3-dmesg2.png、R3-dmesg3.png |

- guest は固まっていない: QMP `info registers` は RIP=ffffffff80373e26 CPL=0 HLT=1（idle）、login・dmesg の入力に応答する（kernel の symbol は未調査）。nvrtx の P2b の待ちだけが終わらない。
- login 後に `poweroff` を打った（R3-poweroff.png に入力まで）。QEMU は自分で終了した。その後の P7 の行は画面が消えて撮れていない（`P7 ok` かは未確認）。次の run の前に host の再起動が要るかも未確認。
- R4 は P2b が通らないので流していない（未実施）。QEMU は止まっている。解析はしていない。
