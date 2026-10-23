<!-- awesome-plan project=zedbsd record=ws286-p003 -->

# ws286-p003 PL の block `plio`（openXC7、openFPGALoader、AHB-AP から読み書き）

- 親: [ws286](../ws.md)、設計: [design.md](../design.md) 版 3 の §5.10・§7.5・§4.3（L4）・§4.5・§8 の p003 の行、R14・R18。
- Queue: Q1 の割当（P1、2026-10-23、worktree `/home/awe/zedBSD-worktrees/p1-zybo-p003`・branch `agent/p1-zybo-p003`、基点 main 8ce002104）。
- 判断: U4（担当が板を直に、host の lock `/tmp/zedbsd-zybo-board.lock`）、U5（RTL は `platform/armv7/zybo/pl/plio/`）、D2（OSS だけ: 「ひとまず、openFPGALoaderやopenxc7など、OSSのみで実装を進めてください。難しいとなったらAMDのツールを導入します。」）。
- 途中の方針の変更（2026-10-23、Q1 経由のユーザーの原文）:「了解です。それならopenxc7は使わなくてもよいかも。RTLを配布物にして、プロジェクト、bitstream は別なツリーでもいいです。あとでリファクタすればいいです。」→ Q1: openXC7 は必須でない、通る見込みなら進めてよい。この時点で openXC7 の build は進んでおり PS7 の対応も確かめていたので、openXC7 のまま進めた（下のとおり通った）。bitstream は tree の外（`build/`）、RTL は tree の中。
- Status: cleared（2026-10-23 Q1 の判定: openXC7 の導入、plio の bitstream、JTAG の書き込み、GP0 の register と IRQ の実機の確認で受け入れを満たした。LED の目視とスイッチ・ボタンは未確認）

## 目的と受け入れの条件

1. host に openXC7（yosys・nextpnr・prjxray の database・frame/bit の道具）を user の場所に入れ、版と方法を記録する。XC7Z010 か PS7 が扱えなければ止める（R14）。
2. `platform/armv7/zybo/pl/plio/` に PS7 の GP0 の AXI3 slave の block（ID・SCRATCH・COUNTER・LED・INPUT・割り込み）、ZYBO の pin の制約、`pl-build.sh` で `build/` に bitstream。SPDX Zlib。
3. 実機: openFPGALoader（`pl-jtag.sh`）で PL を書き、`zybo_pl_enable`、AHB-AP で ID・SCRATCH・LED を読み書き、switch を読み、INT_STS を記録する。
4. 記録と Master の Tools の文。

## 結果の要約

- **R14 は起きなかった**: openXC7（openXC7/nextpnr の himbaechel の xilinx）は XC7Z010 と `PS7` を pack・place・route でき、GP0 の block は実機で動いた。
- 実機（2026-10-23、QEMU は無し）: `plan/ws286/tests/plio-jtag.sh` が 2 回続けて PASS。ID `0x504c494f`、VERSION 1、SCRATCH の書き読み 2 値、COUNTER は約 100 ms で 9994705〜10076635（FCLK0 ≈ 100 MHz）、LED の register の書き読み（0xF → 0x5）、INPUT `0x00000000`、未使用の offset が 0、IRQ: `IRQ_SET` → `IRQ_STATUS` 2、ENABLE 0 で線は 0、ENABLE 2 で **GIC の SPI の状態（PL390 `spi_status` `0xF8F01D04` の bit 29 = ID 61）が 1**、W1C で 0。
- **INT_STS（`0xF800700C`）**: SPL の後・PL の前 `0xa802000a`（bit 2 = 0）、`pl-jtag.sh` の後 `0xf802000f`（**bit 2 PCFG_DONE = 1**、bit 0 も 1）。devcfg `STATUS`（`0xF8007014`）は `0x40000a30` → `0x40000f30`。→ design §5.6 の M4 の「JTAG の書き込みで PCFG_DONE が立つか」は **立つ**（3 回とも同じ値）。`fpgaload --enable --force` は要らない見込み。
- **IRQ_F2P[0] = GIC の ID 61**（design §5.6.2 の「未確認」）を実機で確かめた（上の spi_status の bit）。
- **R18 の材料**: PL が未設定で level shifter が閉じた（`LVL_SHFTR_EN` 0、`FPGA_RST_CTRL` 0x01f33f0f）時に AHB-AP で `0x43C00000` を読むと、`Timeout during WAIT recovery`（1000 ms）で失敗し、その後は AHB-AP が `JTAG-DP STICKY ERROR` で `PSS_IDCODE` も読めなくなる。`jtag-reset.sh` で戻った（どちらの reset の経路を使ったかは記録しなかった）。→ kernel が `ENABLED` の時だけ窓を許す設計（§5.6.2）が要る事の裏付け。CPU で同じ access をした時に止まるかは未確認（試していない）。

## 作った物

| file | 中身 | license |
| --- | --- | --- |
| `platform/armv7/zybo/pl/plio/plio.v` | top `plio`。`PS7` の instance（`FCLKCLK`・`FCLKRESETN`・`IRQF2P`・`MAXIGP0*`）、FCLK0 の `BUFG`、FCLK_RESET0_N の 2 段の同期、AXI3 slave（write は AW の後に W を受け、`BID` = AW の ID、read は AR の時に data を取り保持、burst は先頭の address の register を各 beat に、全部 OKAY、`WSTRB` を守る）、register は design §5.10 の表のとおり（下位 16 bit を解く）。`IRQ_F2P[0]` = OR(STATUS & ENABLE)、button の立ち上がりで STATUS[0] | Zlib |
| `platform/armv7/zybo/pl/plio/plio.xdc` | LD0〜3・SW0〜3・BTN0〜3 の pin（LVCMOS33）、`create_clock -period 10 [get_nets clk]`。Values referenced: Digilent `digilent-xdc` の `Zybo-Master.xdc`（MIT、commit c8644a9fe31c、SHA-256 `b439e8dc629d09ed…`）。pin: SW G15・P15・W13・T16、BTN R18・P16・V16・Y16、LED M14・M15・G14・D18（PL の sysclk は L16 125 MHz と同じ file に有るが plio は FCLK0 を使う） | Zlib |
| `platform/armv7/zybo/pl/plio/README.md` | block の説明と build・試験の道 | （文書） |
| `plan/tools/zybo/pl-build.sh RTL_DIR [BUILD]` | openXC7 で `xc7z010clg400-1` の bitstream。chip database は初回に `BUILD/chipdb/xc7z010.bin` に作る（約 30 s）。yosys `synth_xilinx -flatten -abc9 -arch xc7` → `nextpnr-himbaechel --device xc7z010clg400-1 -o xdc= -o fasm=` → `fpga-as`。利用率と post-route の Fmax を表示。既定の BUILD は `build/zybo-pl`、`OPENXC7` で install の場所 | Zlib |
| `plan/tools/zybo/pl-jtag.sh FILE.bit` | lock を取り `openFPGALoader -c digilent --ftdi-serial 210279785182 --index-chain 1 --write-sram FILE.bit`（index は `--detect` で 0 = Cortex-A9 の DAP、1 = xc7z010 を確かめた）。PS に触らない | Zlib |
| `plan/ws286/tests/plio-tb.v`・`plio-tb.sh` | host の simulation（Icarus Verilog 12.0、Debian `iverilog`）: PS7・BUFG の代わりで GP0 を駆動し 24 項目（reset 中に ARREADY 0、ID・RID・BID、SCRATCH、WSTRB と data 先行の write、COUNTER、LED の pin、INPUT、64 KiB の alias、4 beat の burst、IRQ の set・mask・enable・W1C・button の押下・押したままで再び立たない） | Zlib |
| `plan/ws286/tests/plio-jtag.sh [FILE.bit]` | 実機の試験（design §7.5）: `jtag-boot.sh --spl-only` → INT_STS → `pl-jtag.sh` → INT_STS → `zybo_pl_enable` → AHB-AP で register、GIC の spi_status は止めた CPU0 で読む（private の領域は AHB-AP の bus に無い想定。AHB-AP では試していない）。PASS/FAIL を出す。終わりに LED は 0b0101 | Zlib |

## 実行した command と結果（2026-10-23、P1、host と実機 = ZYBO、QEMU は無し）

### openXC7 の導入（host）

- 方法: openXC7 の `toolchain-installer` の **`toolchain-sources-builder.sh`**（repository の commit `efe9e07330e88434a831f2c52bee72739da0f0a5`、script の SHA-256 `f958a54c2f0add4f…`）。README の推しは Nix か Apio だが、Nix は host の daemon の導入が要るので、source から user の場所へ build する道を選んだ。
  - `sudo apt-get install -y libboost-filesystem-dev libboost-iostreams-dev libboost-thread-dev libboost-program-options-dev libboost-python-dev libeigen3-dev libreadline-dev tcl-dev graphviz xdot pypy3`（builder の依存の不足分。pypy3 の postinst が `unsupported locale setting` で落ち、`sudo env LANG=C.UTF-8 LC_ALL=C.UTF-8 dpkg --configure pypy3` で通した）。host の試験用に `sudo apt-get install -y iverilog`（12.0-2+b1）。
  - `cd build/openxc7-src && LC_ALL=C.UTF-8 INSTALL_PREFIX=/home/awe/opt/openxc7 JOBS=48 bash toolchain-sources-builder.sh all`（rc 0、約 10 分）。builder は自分の clone の中の `build/` と sed の `.bak` を消す（第三者の installer の中の後片付け。担当が直に打った rm は無い）。
- install 先: **`/home/awe/opt/openxc7`**（1.5 GB）。使う時は `. /home/awe/opt/openxc7/export.sh`（PATH、`PRJXRAY_DB_DIR` など）。
- 版（builder の pin、clone の HEAD で確かめた）:
  - yosys v0.69（`9f75ca1f9834a39a863915b5dae0c7b1e33533bc`、`Yosys 0.69 (git sha1 9f75ca1f9, Release, GNU /usr/bin/c++ 14.2.0)`）
  - nextpnr（openXC7/nextpnr、himbaechel の xilinx の uarch。旧 nextpnr-xilinx の後継）`3e5c2cdd256745fb5adb93f3ffe8c3795b1e7abf`（`Version 1.0.0-41-g3e5c2cdd`）。`nextpnr-xilinx` の名前は互換の shim
  - prjxray `ed3331c6200f421164101388759fc2860b0f5634`（`fasm2frames`・`xc7frames2bit` も入った）
  - prjxray-db `517d66a383676cb971177ea92b0ff3b6ea6e8690`（`zynq7` を含む）
  - fpga-assembler（`fpga-as`、fasm2frames + xc7frames2bit の代わり）`cf0e3f08455d502fc6392889c07f482ab8dd2d62`、Bazel 8.5.0（builder が SHA-256 を照合）
- install の外に残った物: `~/.cache/bazel`（444 MB）・`~/.cache/pip`（80 MB）。build の source は worktree の `build/openxc7-src`（2.8 GB、install には要らない）。
- PS7 の可否を build の前に source で確かめた: `himbaechel/uarch/xilinx/pack_io.cc` が `PS7` を `PS7_PS7` に、`pack_clocking.cc` が preplace、`xilinx_place.cc` が未使用の入力を定数に。openXC7 の demo-projects（commit 0e35197c51cd）に `ps7-blinky-digilent-pynqz1`（PS7 + M_AXI_GP0、xc7z020）が有る。

### build（host）

- `plan/ws286/tests/plio-tb.sh` → `PASS plio-tb`（24 項目 ok、warning 0）。
- `plan/tools/zybo/pl-build.sh platform/armv7/zybo/pl/plio`（chip database の生成を含めて 45 s、2 回目から 18 s）。yosys の warning は `ABC: Warning: The network is combinational.`（abc9 の部分の網の通知）だけ。nextpnr の warning は 0。
  - 利用率（nextpnr）: SLICE_LUTX 193/35200、SLICE_FFX 197/35200、CARRY4 10/4400、PAD 12/196、IOB33_OUTBUF 4、IOB33_INBUF_EN 8、BUFGCTRL 1/32、PS7 1/1。yosys の数: FDRE 197、LUT 119（LUT2 10・LUT3 51・LUT4 23・LUT5 20・LUT6 15）、CARRY4 9、INV 5、IBUF 8、OBUF 4、BUFG 1、PS7 1。
  - **Fmax（post-route）: `clk` 248.08 MHz（100 MHz の制約に PASS）**。配置の前の見積もりは 299.76 MHz。
  - 出力 `build/zybo-pl/plio/plio.bit` 2083843 byte。
- 再現性: 同じ RTL を別の BUILD に 2 回 build して FASM は同一、`.bit` は header の日時の 2 byte だけ違う（`fpga-as` が日時を書く）。
- 最初の失敗: XDC の `[get_ports { sw[0] }]`（括弧の内側に空白）で nextpnr の XDC の parser が `Assertion failure: str.back() == '}'`（`xdc.cc:55`）で落ちた → `[get_ports {sw[0]}]` に直した。`pl-build.sh` は nextpnr の stderr を捨てていたので log の末尾しか出なかった（log に assertion は出ない）→ stderr を `BUILD/NAME/nextpnr.err` に取り、失敗の時に両方の末尾を出すように直した。
- SPL: この worktree で `plan/tools/zybo/spl-build.sh` → `u-boot-spl.bin` SHA-256 `ed936c133280ec55…`（**p002 と同じ値**: p002 の残件「同じ source の bit 一致は未確認」は、別の worktree の build で一致した）。

### 実機

- `flock /tmp/zedbsd-zybo-board.lock openFPGALoader -c digilent --ftdi-serial 210279785182 --detect`: index 0 `0x4ba00477` ARM cortex A9、index 1 `0x3722093` xilinx zynq xc7z010（sudo 無しで動いた）。
- `plan/ws286/tests/plio-jtag.sh`: 最初の 2 回は試験の script の誤り（`zybo-common.sh` の cfg の path が `$0` から決まり tests/ を指した、Tcl の文字列の中の `[3:0]` が command の置換になった）で途中まで。直して 2 回続けて PASS（上の要約の値。試した `.bit` の SHA-256 は `286295eb01f18245…`、後の build は header の日時だけ違う）。`pl-jtag.sh` は 3〜4 s（6 MHz、`isc_done 1 … done 1`）。
- GP0 を PL 無しで読む試し（R18、上の要約）: `jtag-boot.sh --spl-only` → `zybo_rd 0x43C00000` が失敗 → AHB-AP が sticky error → `jtag-reset.sh` で `reset done`。

## 未実施・残件

- **LED の目視と switch・button を動かしての INPUT の読み**は未実施（INPUT は全部 0 を読んだだけ。LED は register の読み戻しだけで、光る pin が正しいかは見ていない）。ユーザーが拾える試験の一覧へ（Q1）: 「`plan/ws286/tests/plio-jtag.sh` の後に LD0 と LD2 が点く。SW を動かして OpenOCD で `zybo_rd 0x43C0000C` が変わる、BTN を押すと IRQ_STATUS[0]」。
- PS7 の無い LED の点滅（design §5.10 の step (a)）は作らなかった: (b) の PS7 + GP0 が最初から通ったので不要と判断した。
- GP0 の access を CPU（kernel の道）でした時に PL 無しで止まるかは未確認（R18、p008 の守りの前提）。
- design.md への書き戻しは担当の範囲外として、次を Q1 に推す: §5.6 M4 の「PCFG_DONE が立つかは未確認」→ 立つ（INT_STS `0xf802000f`）、§5.6.2 の「IRQ_F2P[0] = ID 61 は未確認」→ 確認、§5.10 の「openXC7 で PS7 は未確認（R14）」→ 動いた、§4.4 の `pl-jtag.sh` の option（`--ftdi-serial`・`--index-chain 1`・`--write-sram`）、§4.3 L4 の AHB-AP の固まりは GP0 でも起き `jtag-reset.sh` で戻る。
- Q1 に消してほしい物（worktree の build/ の中）: `build/openxc7-src`（2.8 GB、builder の source。install には不要）、`build/zybo-pl/again`・`build/zybo-pl/try`（試しの build）、`build/zybo-xdc`（XDC の取得の写し）。`~/.cache/bazel` は builder の cache（openXC7 を build し直さないなら不要）。
