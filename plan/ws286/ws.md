<!-- awesome-plan project=zedbsd record=ws286 -->

# WS286 Digilent ZYBO（初代、Zynq Z-7010）への移植: ARMv7-A の HAL・BSP・driver

- Master: [plan/master.md](../master.md)（2026-10-23 作成）。Primary Milestone: MG008（HAL の移植と異種機）。
- Status: completed（2026-10-23。全 Phase p001〜p010 が cleared。到達目標「JTAG で起動し、PL の block を zedBSD から動かす」を実機で確認した。完了の判定は Q1）
- 出典: 2026-10-23 ユーザー「次の計画です。DIGILENT ZYBOボードにzedBSDを移植します。Zynq Z-7010のモデルです。」「初代 ZYBO（Z-7010）です。画面はカーネルの移植後に、RTLを実装します。」「FPGAのツールチェインは私の方でホストにインストールします。ひとまずarmv7のHALと、このボードのBSP、ドライバを実装しましょう。JTAGからのブートはできたらうれしいものの、まずはSDブートで、SSHでカーネルを更新して、Switchbotで再起動、というのがいいですね。PLのプログラミングは、SSH経由でできるようにできそうですか？」。のちに「USBは後回しにして、full speed, low speedはWSを作ってあとで対処し、今はFPGAブロックを動作させるということをゴールにして最短で進みましょう。」（到達目標を絞った）、「U項目は読みましたが、全部推しでOKでした。」（設計の決定 U1〜U8）。

## 結果

到達目標（design §7.7）を実機の ZYBO（JTAG で起動、生きた UART）で確認した。

- 新しい arch ARMv7-A（Cortex-A9、VFPv3・NEON、1 CPU）の HAL（`src/hal/armv7/`）、zybo の BSP（`src/hal/armv7/bsp-zybo/`: Cadence UART、SLCR、private/global timer、GIC、SCU と ACTLR.SMP、PSS_IDCODE による QEMU の判別）、QEMU virt の BSP、Q1 の toolchain（armv7-unknown-zedbsd-eabihf）と sysroot の上の動的 PIE の userland（libc・rtld の ARM 版、`__thread`、VFP の lazy な切り替え）。
- 起動: OSS だけ。U-Boot SPL（ps7_init）+ OpenOCD の JTAG で kernel と ramdisk を DDR に置き、reset も JTAG（`plan/tools/zybo/`）。root は ramdisk（`root.img` 5 MiB）、UART の console で `login:` → root → `uname -m` = armv7、`ls`・`cat`・`ps`・`mount`・`reboot`（SLCR の reset）。UART の受信は割り込み。
- PL: openXC7 で作った block `plio`（`platform/armv7/zybo/pl/plio/`、AXI GP0 の register、LED、SW、BTN、PL の割り込み = GIC ID 61）。openFPGALoader（JTAG）と zedBSD の `fpgaload`（devcfg/PCAP、`/dev/fpga0` 0600）の両方で書き、`fpgareg` で ID `0x504c494f`・SCRATCH・COUNTER・LED・IRQ（wait・ack・ETIMEDOUT）を確かめた。`--disable` の後の窓は EIO、JTAG で書いた後の `--enable` も通る。
- 証拠の区別: p003〜p008・p010 の受け入れは実機（JTAG、生きた UART）。p004〜p008・p010 は T1・T2 の QEMU（virt は `boot-test.sh` の PNG、zynq は生きた serial）でも確認。zybo・armv7-virt・amd64・rpi4 の build は warning 0。host 試験 aeabi・pte・devcfg・bitstream が PASS。規約の全文の見直し（p009）は 19 file を書き方だけ直し、動作は変えていない（実機で HAL READY・login・fpgaload・ID・IRQ が不変）。

## 制限・引き継ぎ

- **QEMU 10.0.11（host の Debian）の devcfg の model の限り**: machine の reset が devcfg の `UNLOCK` の post_write を値 0 で呼び、領域を無効にして PCAP の転送を完了できない（upstream の修正 b9b3ca738d は 10.0.11 に無い）。QEMU zynq での p008 の受け入れは、示せる物（`EMULATED 1` の判別、窓の EIO、DONE 無しの `--enable` の EIO、program の timeout の扱いと `error 42` の記録、その後の shell）に縮めた。PCAP の転送の成功は実機だけで確認している。QEMU を更新する時に再確認する。
- **ユーザーの試験 U37**（LED の目視と SW・BTN。[plan/beta2.md](../history/beta2.md)）: LD0 と LD2 が点く、SW を動かすと `fpgareg read 0xc` が変わる、BTN を押すと `fpgareg wait` が戻る。LED の pin の正しさと SW・BTN の入力は register の読み戻しまでしか見ていない（実機の目視は未実施）。
- 別の WS に移した物: SD と SD の起動・LAN と SSH・FIT の起動は [WS289](../ws289/ws.md)。USB（EHCI の split transaction、ZYBO の ChipIdea）は [WS287](../ws287/ws.md)。SwitchBot は ZYBO に未接続（板は USB 給電、reset は JTAG か SLCR）。
- 未着手の残件（今の WS は無い。必要になった時に Q1 が WS を立てる）:
  - PL310（L2 cache）は off のまま。outer cache の操作は無い（design §3.7・§3.11）。
  - SMP の CPU1。2 CPU での VFP の移動（`loaded_cpu`）、BPIALLIS・ICIALLUIS の broadcast、SCU の invalidate。
  - `ptrace.c` の `hal_icache_invalidate_range(user の VA)` は armv7 では user の VA への MVA の maintenance になる（erratum 775420、design §3.3）。armv7 で ptrace を使う時に direct map 経由にするか fault の fixup を足す。
  - armv7 の `struct stat` は `packed, aligned(4)` で `timespec` は 8 byte 揃えなので、`&st.st_mtim` が `-Waddress-of-packed-member` になる（`userland/base/ls/main.c` は値で返す形に直した）。他の command が同じ形を取る時に出る。uapi の `struct stat` の配置が armv7 の ABI として正しいかは未決（i386 は 4 揃えで出ない）。ABI を固める時に判断する。
  - `__thread` を持つ実行 file の link（`-z undefs` + `--provider` の検査）は abitest だけ。riscv64 にも同じ穴がある。
  - 外部 package の openssh は armv7 の build でも upstream の warning を出す（他の arch と同じ既存の物で、我々の source ではない）。
  - PL の mmap の窓（画面の RTL）、`fpga.window=BASE,SIZE`、partial reconfiguration は後の WS（窓は今 `0x43C00000` 64 KiB に固定で plio だけ）。
  - `src/drivers/ethernet/cadence-gem.c` の書き方の指摘（722 行、1659・1676 行）は WS289 の範囲で直す。

## 残した物

- 設計: [design.md](design.md)（版 3、板の事実・JTAG と SPL・メモリの map・devcfg の手順。WS289・WS287 と後の画面の RTL が節番号で引く参照として保つ）、[proposed/hal-armv7.md](proposed/hal-armv7.md)（HAL の事前承認の 3 点の案。[Guardrail](../guardrail.md) の表が引く）。
- 道具（Master の Tools に登録）: `plan/tools/zybo/`（`jtag-reset.sh`・`jtag-probe.sh`・`jtag-boot.sh`・`spl-build.sh`・`uart-expect.py`・`pl-build.sh`・`pl-jtag.sh`・`qemu-zynq.sh`・`zybo-common.sh`・`openocd-zybo.cfg`）に、WS から移した `plio-jtag.sh`（板の試験: SPL → PL → GP0 の register と IRQ ID 61）と `config-zybo-fpga.mk`（fpgaload・fpgareg・`plio.bit` を入れた試験の config）。`boot-test.sh` の `BOOT_MODE=armv7-virt`。
- 試験の移動（WS の tests/ は削除）: `platform/armv7/tests/abitest.c`（armv7 の ABI の試験、`platform/armv7/userland.mk` が `abi.img` に build）、`platform/armv7/zybo/pl/tests/plio-tb.sh`・`plio-tb.v`（plio の host の simulation、iverilog）、`tests/aeabi-host-test.c`・`tests/armv7-pte-host-test.c`・`tests/zynq-devcfg-host-test.c`・`tests/fpgaload-bitstream-host-test.c`（host の unit test。実行の command は各 file の冒頭の comment）。削除した物: `jtag-hello.S`・`jtag-hello.sh`（kernel が無い時の JTAG の起動の端から端の確かめ。kernel が JTAG で起動するようになり、`jtag-boot.sh` の実利用が代わった開発用）。
- 削除した Phase の記録・review・p001 の対応の表・各 Phase の「design との違い」は、main 463911ab9 の git の履歴にある（`plan/ws286/phase001`〜`phase010`、`plan/ws286/proposed/boot-test-armv7.md`、`plan/ws286/tests/`）。

## Phase

| Phase | 内容 | LW | 状態 |
| --- | --- | --- | --- |
| p001 | 調べと設計（[design.md](design.md) 版 3。敵対的 review の全指摘に対応） | 14 | cleared（2026-10-23） |
| p002 | JTAG の道具と U-Boot SPL、板の probe（DDR の照合、load 約 255 KiB/s） | 8 | cleared（2026-10-23、実機） |
| p003 | PL の block `plio`（openXC7、openFPGALoader、AHB-AP から読み書き） | 8 | cleared（2026-10-23、実機。LED の目視と SW・BTN は U37） |
| p004 | HAL の入口（QEMU virt、1 CPU、HAL READY・AEABI・ATOMIC・PAGING・TIMER TICK） | 16 | cleared（2026-10-23、T1 の QEMU virt） |
| p005 | zybo の BSP を JTAG で（UART、SLCR、timer、GIC、SCU、`hal_pmem_map_uncached`） | 12 | cleared（2026-10-23、実機と T1 の QEMU zynq・virt） |
| p006 | task・syscall・signal・VFP・cache、kernel の platform（VFP PASS、ramdisk） | 14 | cleared（2026-10-23、実機と T1 の QEMU） |
| p007 | userland の runtime（sysroot、crt、libc、rtld の ARM 版、ABI PASS） | 20 | cleared（2026-10-23、実機と T1 の QEMU） |
| p010 | ramdisk の root と login（`uname -m` = armv7） | 8 | cleared（2026-10-23、実機と T2 の QEMU） |
| p008 | PL を zedBSD から（devcfg、`/dev/fpga0`、`fpgaload`・`fpgareg`、割り込み）= 到達目標 | 13 | cleared（2026-10-23、実機。QEMU は縮めた受け入れ） |
| p009 | 規約の全文の見直し（19 file、動作は不変） | 6 | cleared（2026-10-23） |

合計 119 LW。
