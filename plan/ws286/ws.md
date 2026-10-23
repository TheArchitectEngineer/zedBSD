<!-- awesome-plan project=zedbsd record=ws286 -->

# WS286 Digilent ZYBO（初代、Zynq Z-7010）への移植: ARMv7-A の HAL・BSP・driver

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。
- Status: incomplete（p001 の設計を実行中。2026-10-23 P1: record-audit の WS_NOT_STARTED_BUT_EXEC を直した）
- 出典: 2026-10-23 ユーザー「次の計画です。DIGILENT ZYBOボードにzedBSDを移植します。Zynq Z-7010のモデルです。」「初代 ZYBO（Z-7010）です。画面はカーネルの移植後に、RTLを実装します。」「FPGAのツールチェインは私の方でホストにインストールします。ひとまずarmv7のHALと、このボードのBSP、ドライバを実装しましょう。JTAGからのブートはできたらうれしいものの、まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。PLのプログラミングは、SSH経由でできるようにできそうですか？」

## 目的と完了の条件（案、版 2）

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
| p001 | 調べと設計（[design.md](design.md) 版 2。U1・U3〜U8 の判断と review 待ち） | — | in-progress |
| [p002](phase002/phase.md) | JTAG の道具と SPL（kernel 無し、LLVM 不要）、板の probe（8 LW） | U4 | cleared（2026-10-23 Q1: 実機で SPL の banner・DDR の 8 MiB の照合・JTAG の reset・load 約 255 KiB/s、jtag-hello.sh PASS） |
| p003 | PL の block `plio`（openXC7、openFPGALoader、OpenOCD から読み書き）（8 LW） | p002、openXC7 の導入、U5 | planning |
| p004 | HAL の入口（virt、1 CPU、PNG で HAL READY・PAGING PASS・TIMER TICK）（14 LW） | Q1 の LLVM ARM、boot-test の mode、U1 | planning |
| p005 | zybo の BSP を JTAG で（QEMU zynq と実機の UART）（10 LW） | p002、p004、U3、serial.py --device | planning |
| p006 | task・syscall・signal・VFP・cache（VFS initialization failed）（14 LW） | p005 | planning |
| p007 | userland と ramdisk の root（login:）（16 LW） | p006、Q1 の toolchain 案 2・sysroot | planning |
| p008 | PL を zedBSD から（devcfg、/dev/fpga0、fpgaload・fpgareg、割り込み）= 到達目標（12 LW） | p003、p007、devfs の fpga、U6 | planning |
| p009 | 規約の全文の見直し（5 LW） | p002〜p008 | planning |

合計 97 LW（p001 の 10 を含む）。critical path: Q1 の LLVM → p004 → p005 → p006 → p007 → p008 → p009。p002 は今すぐ、p003 は openXC7 が入れば並行。

## 現在の状態・再開点

- 2026-10-23: 作成。p001 の設計から。
- 2026-10-23: 実機の接続。ZYBO の USB（FT2232H、Digilent Adept、serial 210279785182）を host に接続: /dev/ttyUSB0 = if00（JTAG）、/dev/ttyUSB1 = if01（UART、115200、console）。電源は SwitchBot のプラグ AC276E42B252（VF2 から付け替え、ユーザー「AC276E42B252をプラグします。認識の通りです。」）。boot mode の jumper は今は JTAG。host の user awe を dialout に追加（次の login から sudo 無しで読める）。
- 2026-10-23: OSS だけで進める（ユーザー）。openocd・openFPGALoader を host に導入、`sudo openFPGALoader -c digilent --detect` で Cortex-A9 の DAP と xc7z010 を確認。ZYBO は USB 給電、SwitchBot は未接続（JTAG で reset する）。
- 2026-10-23 P1: design.md 版 1（§0〜§11）。ユーザーの決定（JTAG の起動を SD より先、OSS だけ、SwitchBot は未接続）は design §12 に原文で置き、反映は引き継ぎの担当（§4・§8・§10 の書き直し）。
- 2026-10-23 P1（worktree p1-zybo2）: design.md 版 2。§12 の決定（OSS だけ、JTAG が先、JTAG で reset、SwitchBot 無し）と、到達目標を PL の block に絞る決定（USB は外す）を反映。§4（JTAG の流れ、SPL の JTAG での振る舞い、OpenOCD の cfg、道具）、§5.6・§5.10（devcfg、窓、block）、§7〜§10、§12 を書き直し、`proposed/hal-armv7.md`・`proposed/boot-test-armv7.md` を書いた。再開点: U1・U3〜U8 の判断、review、Q1 の LLVM。p002 は判断 U4 の後すぐ始められる。
- 2026-10-23: ユーザー「U項目は読みましたが、全部推しでOKでした。」 → U1・U3〜U7 は推しで決定（Master の decisions-log、AGENTS.md の検証の例外、Guardrail の表）。
- 2026-10-23 P1（worktree p1-zybo-p002）: p002 を実機で実行。`plan/tools/zybo/`（openocd-zybo.cfg・jtag-reset/probe/boot.sh・spl-build.sh・uart-expect.py）、U-Boot v2025.07 の SPL（build/ の中、GPL）、SPL の banner と breakpoint、DAP → SLCR の reset、DDR の 8 MiB の書き読みと速さ（約 255 KiB/s）、`plan/ws286/tests/jtag-hello.sh` PASS。結果は [phase002](phase002/phase.md)、design §4.8。再開点: Q1 の p002 の判定と Tools の登録。p003 は openXC7 の導入待ち。
