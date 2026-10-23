<!-- awesome-plan project=zedbsd record=ws288-kc705-bringup -->

# WS288 KC705 の workflow の確認（bringup）

- 親: [ws.md](ws.md)。ws.md の Phase の表の p001（設計）とは別の、KC705 で FPGA の workflow を確かめる作業の記録（番号の割り当ては Q1）。
- 状態: 板で確認済み（2026-10-23 P1、worktree p1-kc705）。判定は Q1。
- 対象: `platform/fpga/kc705/bringup/`（bringup.v・bringup.xdc）、`plan/tools/kc705/`、`plan/ws288/tests/bringup-tb.sh`。

## 設計

- 200 MHz の SYSCLK（AD12/AD11、DIFF_SSTL15）、CPU_RESET、DIP 4 個、push button 5 個（N/E/S/W/C）、LED 8 個。
- LED[3:0] は DIP、LED[7:4] は 0.34 s ごとに動く walking one（C を押すと止まり、N/E/S/W のどれかを押す間は {N,E,S,W} を表示）。
- JTAG の BSCANE2（USER1、IR 0x02、128 bit）: ID `0x4B373035`（"K705"）、STATUS、Gray code の COUNTER、SCRATCH（key `WRIT` で書く、bit 31 で LED に表示）。

## 結果（板、KC705 = Digilent FT2232H serial 210203368704、ZYBO には触れていない）

| 確認 | 結果 |
| --- | --- |
| openXC7 の bitstream を `jtag-load.sh` で書く | rc 0、16 s |
| `jtag-read.sh` | ID `4b373035`、COUNTER は 100 ms で約 2.0e7（200 MHz）、PASS |
| SCRATCH の書き込み（`--write 0x800000a5`） | LED 0xA5、boundary scan でも AB8=1・AA8=0・AC9=1・AB9=0。ユーザーの目視「LEDはok」 |
| button を押していない時の入力（直す前） | S（AB12）だけが 1、walking one が止まる（ユーザー「Sw sは存在しないかも？SW4かな？論理が逆では？」） |
| 同（直した後、openXC7・Vivado の両方） | buttons 0x00、DIP 0x0、LED[7:4] が 0x10 → 0x20 → 0x40 と進む |
| `plan/ws288/tests/bringup-tb.sh`（iverilog） | PASS |

## button の S が 1 のままだった原因

- pin と極性は正しかった。出典: Vivado 2026.1 の board file `data/xhub/boards/XilinxBoardStore/boards/Xilinx/kc705/1.7/`。
  `part0_pins.xml`: GPIO_SW_N AA12・GPIO_SW_E AG5・GPIO_SW_S AB12・GPIO_SW_W AC6（LVCMOS15）、GPIO_SW_C G12（LVCMOS25）。
  `board.xml` の push_buttons_5bits: 「Push Buttons, C W E S N, Active High」。電源の bank: AA12・AB12 は bank 33、AG5・AC6・AB7（CPU_RESET）は bank 34（どちらも 1.5 V の HP）。G12 は 2.5 V の bank（prjxray の package_pins では bank 18）。
- 基板の SW 番号（SW2〜SW6 のどれが S か）は UG810 を取得できず確かめていない（未確認）。ユーザーの「SW4」は GPIO_SW_S の候補として妥当だが、記録上は未確認。
- boundary scan（新しい道具 `jtag-sample.sh`、SAMPLE は動いている design を乱さない）で AB12 の pad は 0。fabric だけが 1 を読んでいた → openXC7 の IO の設定の誤り。
- 原因の箇所: nextpnr-himbaechel 3e5c2cdd の `himbaechel/uarch/xilinx/fasm.cc` `write_io_config` の `is_right_hp_single_ended_input_master`。RIOB18（右の HP bank）で P 側（IOB_Y0）の入力に `LVCMOS12_LVCMOS15.IN` を書き（prjxray の segbits ではこれが IOB_Y1 の `.IN` の bit 39_01 を落とす）、相方の IOB_Y1 が入力でも `PULLTYPE.PULLDOWN` を無条件に書く。N 側（IOB_Y1、AB12）には `.IN` が無く、入力の受け口が無効のまま 1 を読む。AA12 と AB12 は同じ tile（RIOB18_X95Y97）の対で、両方が入力の時だけ起きる。
- 回避: `plan/tools/kc705/fasm-hp-input-pair.awk`（`oss-build.sh` が nextpnr の `NAME.raw.fasm` に掛けて `NAME.fasm` を作る）。両半分が LVCMOS の純粋な入力の RIOB18 の tile で、各半分に自分の `LVCMOS12_LVCMOS15_LVCMOS18.IN` を付け、IOB_Y1 の pulldown を外す。
- 正しさの根拠: Vivado の bitstream を prjxray の bit2fasm で戻すと、RIOB18_X95Y97 の feature は直した openXC7 の bitstream と同じ（両半分の `.IN`、両方 PULLTYPE.NONE）。IOB の残りの差は LED の DRIVE の alias、sysclk の `ZIBUF_LOW_PWR`・`IBUFDS_BANK_GLUE`、RIOB18_X95Y125 の IOB_Y1 の `IBUF_HP_BANK_GLUE` だけで、板の動作には差が出ていない。
- upstream（openXC7/nextpnr）への報告は未実施（Q1・ユーザーの判断）。
- 2026-10-23 Q1 の確認: upstream では直っている。openXC7/nextpnr の PR #102「xilinx: write each half's .IN when both halves of an HP input tile receive」（2026-10-13 merge）と #106（RIOB18 の pad、2026-10-14 merge）。main（96809c1a、2026-10-22）の `write_io_config` は短い `LVCMOS12_LVCMOS15.IN` を書かず、相方が使われている時は pulldown を書かない。導入済みの 3e5c2cdd（2026-10-05）はその前。→ 報告は不要。openXC7 の nextpnr を新しくした後に awk の回避が不要になるかを確かめ、要らなければ外す（残件）。

## 使用量・timing（同じ RTL・XDC）

| | openXC7（yosys 0.69、nextpnr 3e5c2cdd、prjxray-db 517d66a3） | Vivado 2026.1 batch（BASIC の 4 feature の license） |
| --- | --- | --- |
| LUT | SLICE_LUTX 256（LUT1 122・LUT2 34・LUT3 5・LUT4 5・LUT5 88・LUT6 2） | 82 |
| FF | 282 | 286 |
| CARRY4 | 16 | 15 |
| IOB / BUFG / BSCAN | 20 / 2 / 1 | 20 / 2 / 1 |
| timing | Fmax 推定 clk 229 MHz（200 MHz で PASS）、tck 365 MHz（30 MHz で PASS） | sysclk WNS 3.178 ns・WHS 0.146 ns、tck WNS 31.142 ns・WHS 0.123 ns |
| 時間 | 約 34 s（chip database は作成済み） | 128 s |
| bitstream | 11443716 byte | 11443811 byte |
| 警告 | nextpnr の warning の行 6 | WARNING 0・CRITICAL 0。DRC の CFGBVS-1（CFGBVS・CONFIG_VOLTAGE の未設定）1 件 |

- xc7k325t は BASIC の license で合成・配置配線・bitstream まで通った（log「A valid Vivado Design Suite BASIC license has been detected」）。license の file は Vivado が読むだけで、写していない。
- Vivado の bitstream も板で同じ結果（ID・COUNTER・SCRATCH 0xA5・buttons 0x00・walking one）。

## Vivado を Debian 13 で動かす時の注意（vivado-build.sh に入れた）

- `XILINXD_LICENSE_FILE` は未設定なら `$HOME/Xilinx.lic`。
- Debian 13 に `libncurses.so.5` が無く、Tcl の起動で `libxv_commontasks.so` の load が失敗する。Vivado の `lib/lnx64.o/Ubuntu/24`（ncurses 6 を旧い名前で置いた物）を `LD_LIBRARY_PATH` の先頭に足す。
- 標準入力が端末・socket だと、失敗の後に Vivado が入力を待って止まる（前の担当の 10 分の停止はこれ）。`</dev/null` にした。

## 実行したコマンド

```
plan/tools/kc705/oss-build.sh platform/fpga/kc705/bringup        # build/kc705/oss/bringup/bringup.bit sha256 519221ebda436b26
plan/tools/kc705/vivado-build.sh platform/fpga/kc705/bringup     # build/kc705/vivado/bringup/bringup.bit sha256 06f1b3ae3ad90a42
plan/tools/kc705/jtag-load.sh BIT; plan/tools/kc705/jtag-read.sh [--write 0x800000a5|0]
plan/tools/kc705/jtag-sample.sh AA12 AB12 AG5 AC6 G12 AB7 Y29 W29 AA28 Y28 AB8 AA8 AC9 AB9
bit2fasm.py（openXC7 の venv）で両方の bitstream を FASM に戻して IOB の feature を比べた
plan/ws288/tests/bringup-tb.sh
```

## 未実施・残件

- 2026-10-23 ユーザーの目視「LEDは点滅していました。OKです」（Q1 の解釈: LED[7:4] の walking one が動いている事の確認。button・DIP の確認ではない）。
- button を押した時に 1 になることの確認（N/E/S/W/C、DIP の各 bit）はユーザーの目視が要る（未実施）。押すと LED[7:4] に {N,E,S,W} が出る、C で walking one が止まる、DIP で LED[3:0]。
- 基板の SW 番号と GPIO_SW_* の対応（UG810）は未確認。
- XDC の CFGBVS・CONFIG_VOLTAGE（KC705 の bank 0 の電圧の出典を確かめてから）。
- ~~openXC7 の upstream への報告~~ → upstream で修正済み（上）。nextpnr を更新したら `fasm-hp-input-pair.awk` を外せるか確かめる。

## Master の Tools に載せる文（案、Q1 が登録）

- `plan/tools/kc705/`: KC705（XC7K325T）の FPGA の道具。`oss-build.sh RTL_DIR`（openXC7、HP の入力の対の FASM の回避 `fasm-hp-input-pair.awk` を含む）、`vivado-build.sh RTL_DIR`（Vivado 2026.1 の batch、BASIC の license）、`jtag-load.sh BIT`（openFPGALoader で SRAM に書く）、`jtag-read.sh [--write V]`（bringup の USER1 の register を読む）、`jtag-sample.sh PIN...`（boundary scan の SAMPLE で pad の値を読む）。どれも KC705 の JTAG を serial 210203368704 で選び、`/tmp/zedbsd-kc705-board.lock` を取る。
