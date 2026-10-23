<!-- awesome-plan project=zedbsd record=ws286 -->

# WS286 Digilent ZYBO（初代、Zynq Z-7010）への移植: ARMv7-A の HAL・BSP・driver

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。
- Status: incomplete（p001 の設計は版 3、p002 は cleared、p003 は実機で PASS・Q1 の判定待ち。2026-10-23 P1: record-audit の WS_NOT_STARTED_BUT_EXEC を直した）
- 出典: 2026-10-23 ユーザー「次の計画です。DIGILENT ZYBOボードにzedBSDを移植します。Zynq Z-7010のモデルです。」「初代 ZYBO（Z-7010）です。画面はカーネルの移植後に、RTLを実装します。」「FPGAのツールチェインは私の方でホストにインストールします。ひとまずarmv7のHALと、このボードのBSP、ドライバを実装しましょう。JTAGからのブートはできたらうれしいものの、まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。PLのプログラミングは、SSH経由でできるようにできそうですか？」

## 目的と完了の条件（版 3、到達目標は版 2 と同じ）

- 2026-10-23 ユーザー（Q1 経由）:「USBは後回しにして、full speed, low speedはWSを作ってあとで対処し、今はFPGAブロックを動作させるということをゴールにして最短で進みましょう。」→ 到達目標を絞った（[design.md](design.md) 冒頭・§12）。
- 新しい arch: ARMv7-A（Cortex-A9、VFPv3・NEON）の HAL（1 CPU）と toolchain・sysroot（Q1 の LLVM の変更）。
- 起動: **JTAG**（OpenOCD + U-Boot SPL の ps7_init、reset も JTAG）。root は JTAG で置く ramdisk、操作は UART の shell。道具は OSS だけ。
- ZYBO の BSP と driver: Cadence UART、SLCR、private/global timer、GIC、devcfg/PCAP（`/dev/fpga0`、`fpgaload`・`fpgareg`）。
- PL: openXC7 で作る小さな block（AXI GP0 の register・LED・switch・button・PL の割り込み、`platform/armv7/zybo/pl/plio/` の案）を、openFPGALoader（JTAG）と zedBSD の `fpgaload` の両方で書き、zedBSD から読み書きする。
- 完了: 実機で JTAG から起動して UART に `login:`、`fpgaload` で block を書き、`fpgareg` で ID・register・counter・割り込みを確かめる（design §7.7）。
- WS286 の外（Q1 が別の WS に）: SMP の CPU1、L2、SD と SD の起動、LAN と SSH、USB（split は WS287）。SwitchBot は使わない（未接続）。

## Phase

| Phase | 内容 | 依存 | 状態 |
| --- | --- | --- | --- |
| p001 | 調べと設計（[design.md](design.md) 版 3。review の全指摘に対応、[phase001](phase001/phase.md) の対応の表）（14 LW） | — | in-progress（版 3、Q1 の判定待ち） |
| [p002](phase002/phase.md) | JTAG の道具と SPL（kernel 無し、LLVM 不要）、板の probe（8 LW） | U4 | cleared（2026-10-23 Q1: 実機で SPL の banner・DDR の 8 MiB の照合・JTAG の reset・load 約 255 KiB/s、jtag-hello.sh PASS） |
| [p003](phase003/phase.md) | PL の block `plio`（openXC7、openFPGALoader、AHB-AP から読み書き）（8 LW） | p002、openXC7 の導入、U5 | cleared（2026-10-23 Q1: openXC7 で bitstream、JTAG で PL を書き、ID・SCRATCH・COUNTER・LED・IRQ（ID 61）を実機で確認、plio-jtag.sh 2 回 PASS。LED の目視とスイッチ・ボタンの入力は未確認でユーザーの試験の一覧へ） |
| [p004](phase004/phase.md) | HAL の入口（virt、1 CPU、raw の vmunix.bin を QEMU の Linux の約束で、PNG で HAL READY・AEABI・ATOMIC・PAGING PASS・TIMER TICK）（16 LW） | Q1 の LLVM ARM と triple（cache asset の upload はユーザー）、boot-test の mode（版 3 の案）、U1 | cleared（2026-10-23 Q1: QEMU の virt で T1 の Run A（PNG で HAL READY・AEABI・ATOMIC・PAGING・TIMER TICK）と Run B（r2 の DTB・SVC）が PASS、amd64・rpi4 の build warning 0。hal_pmem_map_uncached は p006 までに） |
| [p005](phase005/phase.md) | zybo の BSP を JTAG で（QEMU zynq と実機の UART、PSS_IDCODE の判別、SCU・ACTLR.SMP、先頭 1 MiB を使わない）（12 LW） | p002、p004、U3 | cleared（2026-10-23 Q1: 実機の ZYBO（JTAG、生きた UART）で HAL READY・TIMER 10S（+0.09%）・3 回続けて VFS の段まで、T1 の QEMU zynq（最終の d7c1d0fbf、exit 0、TIMER 0S→10S 9.982 s、counter 100 MHz）と virt（boot-test PASS、PNG build/review/ws286-p005-virt-final.png）、zybo・virt・amd64・rpi4 の build warning 0。merge 5cd134061） |
| [p006](phase006/phase.md) | task・syscall・signal・VFP・cache（VFP PASS と VFS initialization failed。user mode の受け入れは p007）（14 LW） | p005、U3 | cleared（2026-10-23 Q1: 実機の ZYBO（JTAG、生きた UART）で VFP PASS registers=32・HAL READY … VFP PASS・UART1 の console・ramdisk ram0 の登録・VFS の段まで、T1 の QEMU zynq（A・A2 exit 0）と virt（B・B2 boot-test PASS、PNG build/review/ws286-p006-virt.png）、zybo・virt・amd64・rpi4 の build warning 0。merge c5a0be47f。UART の受信割り込みは p010 で） |
| [p007](phase007/phase.md) | userland の runtime（動的 PIE: sysroot、crt、libc の arch の file、rtld の ARM 版、abitest で ABI PASS）（20 LW） | p006、Q1 の toolchain 案 2・sysroot、U3 | test-wait（T1 の QEMU zynq・virt。実機の ZYBO（JTAG、生きた UART）で abitest の 8 step と ABI PASS、zybo・virt・amd64・rpi4 の build warning 0、2026-10-23 P2） |
| p010 | ramdisk の root と login（login: → root → uname -m = armv7、virt・zynq・実機）（8 LW、版 3 で p007 から分けた） | p007、U3 | planning |
| p008 | PL を zedBSD から（devcfg、/dev/fpga0、fpgaload・fpgareg、割り込み）= 到達目標（13 LW） | p003、p010、devfs の fpga、U3、U6 | planning |
| p009 | 規約の全文の見直し（6 LW） | p002〜p008、p010 | planning |

合計 119 LW（p001 の 14 と cleared の p002 の 8 を含む、残り 97 LW。版 2 の 97 LW から +22: H4 の rtld・libc の分（p007 +4、p010 +8）、影の table・aeabi・self-test・SCU など）。critical path: Q1 の LLVM（ユーザーの cache の upload を含む）→ p004 → p005 → p006 → p007 → p010 → p008 → p009。p003 は openXC7 が入れば並行。

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から。
- 2026-10-23: 実機の接続。ZYBO の USB（FT2232H、Digilent Adept、serial 210279785182）を host に接続: /dev/ttyUSB0 = if00（JTAG）、/dev/ttyUSB1 = if01（UART、115200、console）。電源の記録（版 3 で 1 つの事実に直した、review M10）: この時点の記録は「SwitchBot のプラグ AC276E42B252（VF2 から付け替え、ユーザー「AC276E42B252をプラグします。認識の通りです。」）」だったが、同日の後のユーザーの言（design §12 D2「SwitchbotはまだZYBOに接続されていません。USB給電になっています。」）と Q1 の確認（2026-10-23: プラグ AC276E42B252 は ZYBO に未接続、板は USB 給電で JTAG で reset、ユーザーは以前 VF2 から移すと言った）により、**今の事実はプラグ AC276E42B252 は ZYBO に繋がっていない**。どの道具も ZYBO のためにこのプラグで電源を切らない（移した時はユーザーの言で記録を改める）。boot mode の jumper は今は JTAG。host の user awe を dialout に追加（次の login から sudo 無しで読める）。
- 2026-10-23: OSS だけで進める（ユーザー）。openocd・openFPGALoader を host に導入、`sudo openFPGALoader -c digilent --detect` で Cortex-A9 の DAP と xc7z010 を確認。ZYBO は USB 給電、SwitchBot は未接続（JTAG で reset する）。
- 2026-10-23 P1: design.md 版 1（§0〜§11）。ユーザーの決定（JTAG の起動を SD より先、OSS だけ、SwitchBot は未接続）は design §12 に原文で置き、反映は引き継ぎの担当（§4・§8・§10 の書き直し）。
- 2026-10-23 P1（worktree p1-zybo2）: design.md 版 2。§12 の決定（OSS だけ、JTAG が先、JTAG で reset、SwitchBot 無し）と、到達目標を PL の block に絞る決定（USB は外す）を反映。§4（JTAG の流れ、SPL の JTAG での振る舞い、OpenOCD の cfg、道具）、§5.6・§5.10（devcfg、窓、block）、§7〜§10、§12 を書き直し、`proposed/hal-armv7.md`・`proposed/boot-test-armv7.md` を書いた。再開点: U1・U3〜U8 の判断、review、Q1 の LLVM。p002 は判断 U4 の後すぐ始められる。
- 2026-10-23: ユーザー「U項目は読みましたが、全部推しでOKでした。」 → U1・U3〜U7 は推しで決定（Master の decisions-log、AGENTS.md の検証の例外、Guardrail の表）。
- 2026-10-23 P1（worktree p1-zybo-p002）: p002 を実機で実行。`plan/tools/zybo/`（openocd-zybo.cfg・jtag-reset/probe/boot.sh・spl-build.sh・uart-expect.py）、U-Boot v2025.07 の SPL（build/ の中、GPL）、SPL の banner と breakpoint、DAP → SLCR の reset、DDR の 8 MiB の書き読みと速さ（約 255 KiB/s）、`plan/ws286/tests/jtag-hello.sh` PASS。結果は [phase002](phase002/phase.md)、design §4.8。再開点: Q1 の p002 の判定と Tools の登録。p003 は openXC7 の導入待ち。
- 2026-10-23 P2（worktree p2-zybo-d3）: design.md 版 3。review（Critical 2・High 5・Medium 11・Low 7）の全部に対応（対応の表は [phase001](phase001/phase.md)）。p002 の実測（main 02a1b7837、§4.8）を merge して反映。主な変更: platform ごとの物理の base（zybo 0、virt 0x40000000）、virt は raw の vmunix.bin を QEMU の Linux の約束で（r2・initrd が JTAG と同じ形で渡る）、PSS_IDCODE で QEMU を判別、影の page table、SCU と ACTLR.SMP を 1 CPU でも、動的 PIE の userland と rtld の ARM 版（p007 20 LW）、login を p010 に分けた、devcfg の手順の直し。HAL の API の追加の変更は無し。`proposed/boot-test-armv7.md` は版 3（raw の image）。再開点: Q1 の p001 の判定、Q1 の LLVM（ARM と triple、cache asset の upload はユーザー）、p003 は openXC7 待ち。
- 2026-10-23: 共有の LLVM に ARM の target と armv7-unknown-zedbsd-eabihf、patch level zedbsd11（main ab43a07f0）。cache の asset build/releases/zedbsd-llvm-23.1.0-zedbsd11-x86_64-linux.tar.gz（134612899 byte、sha256 42903d19…）は GitHub の rev-0 への upload がユーザーの確認待ち（upload までは push すると CI の toolchain-cache が失敗する）。
- 2026-10-23 P1（worktree p1-zybo-p003）: p003 を実行。openXC7（toolchain-installer の source builder、yosys 0.69・openXC7/nextpnr 3e5c2cdd・prjxray-db 517d66a3・fpga-as）を `/home/awe/opt/openxc7` に導入。R14 は起きず、`platform/armv7/zybo/pl/plio/`（plio.v・plio.xdc・README）が PS7 + GP0 で通った（LUT 193・FF 197、Fmax 248 MHz）。`plan/tools/zybo/pl-build.sh`・`pl-jtag.sh`、試験 `plan/ws286/tests/plio-tb.sh`（iverilog、PASS）・`plio-jtag.sh`（実機、PASS ×2）。INT_STS は JTAG の書き込みで PCFG_DONE が立つ（`0xf802000f`）、IRQ_F2P[0] は GIC ID 61、PL 無しの GP0 の AHB-AP の読みは AHB-AP を固める（`jtag-reset.sh` で戻る）。結果は [phase003](phase003/phase.md)。途中のユーザーの方針（openXC7 は必須でない、bitstream は別の tree でよい）は phase003 に原文。再開点: Q1 の p003 の判定、design への書き戻し（phase003 の残件）、LED・switch の目視はユーザーの一覧。
- 2026-10-23: ユーザーが Vivado 2026.1 を ~/opt/amd に導入（ユーザー「~/opt/amdにVivadoを入れました。必要なときに使ってください。」）。使う時は `. ~/opt/amd/2026.1/Vivado/settings64.sh`（Vitis も 2026.1）。openXC7 は /home/awe/opt/openxc7。
- 2026-10-23 P2（worktree p2-zybo-p004）: 誤って止められた p004 の作業を Q1 が救った commit（3b7ccc090・4db317270）から続けた。HAL・bsp-virt・platform/armv7 は build（warning 0）・host 試験 PASS。boot-test.sh に `armv7-virt` を足し、table の切り替えの順と locore の DTB の確かめを直した（da63481dd 以降）。T1 の QEMU virt で `ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK` と gdb の確かめが PASS。再開点: Q1 の p004 の判定と merge、次は p005（[phase004](phase004/phase.md)）。
- 2026-10-23 P2（worktree p2-zybo-p005）: p005 を実行。bsp-zybo（Cadence UART、SLCR、PSS_IDCODE の判別、private/global timer、SCU と ACTLR.SMP|FW、PL310 は off、先頭 1 MiB を使わない）、`config/ci/config-zybo.mk`、`qemu-zynq.sh`、`hal_pmem_map_uncached`。QEMU zynq（T1）と実機（JTAG）で `ARMV7 HAL READY AEABI PASS ATOMIC PASS PAGING PASS TIMER TICK`、TIMER 10S は実機 10.009 s・QEMU 10.002 s、VFS の段まで。結果は [phase005](phase005/phase.md)。再開点: Q1 の p005 の判定と merge、uncached の追加の QEMU 再確認（T1）、次は p006。
- 2026-10-23 P2（worktree p2-zybo-p006）: p006 を実行。VFP/NEON の lazy な切り替え（`fpu.c`・`fpregs.S`、sync・forget、Undefined のやり直し、self-test `VFP PASS`）、I-cache の VIPT の扱い、kernel の platform（`armv7-virt.c`・`zybo.c`、`pl011.c`・`cdns-uart.c`、ramdisk の boot device）。実機（JTAG）で `ARMV7 VFP PASS`・`… TIMER TICK VFP PASS`・`VFS initialization failed`、64 KiB の ramdisk で `ram0` が boot device に。結果は [phase006](phase006/phase.md)。再開点: T1 の QEMU zynq（A・A2）と virt（B・B2）、Q1 の判定と merge、次は p007。
- 2026-10-23 P2（worktree p2-zybo-p007）: p007 を実行。armv7 の sysroot（Q1 の許可の範囲で sysroot.mk に armv7 と builtin の manifest）、crt0/crt1・syscall・setjmp、libc の枝、rtld の ARM 版（REL、R_ARM_*、variant I の動的 TLS、TPIDRURO）、`EF_ARM_ABI_FLOAT_HARD` の確かめ、`platform/armv7/userland.mk`（libc.so・ld.so・abitest・abi.img）、`make-armv7-root-image.py`、`check-dynamic-elf.py` の armv7 と `--provider`、`limits.h` の CHAR_MAX。実機（JTAG）で `ABI 1`〜`8 … ok` と `ABI PASS`。結果は [phase007](phase007/phase.md)。再開点: T1 の QEMU zynq（A）と virt（B）、Q1 の判定と merge、次は p010。
