<!-- awesome-plan project=zedbsd record=ws288-p005 -->

# WS288 p005 DC（bitstream 2）: VGA → TMDS、TEST_PATTERN → DDR の framebuffer

- 親: [ws.md](../ws.md)。設計: [design.md](../design.md) §1.3（pclk・pclk5）、§4.2（DC の memory の口 = HP1）、§5（DC）、§8 の p005 の行。
- 担当: P3（2026-10-23、Q1 の依頼）。状態: in-progress（host と板の機械の確認は全部 PASS。**残りはユーザーの目視**（下の「ユーザーの目視の手順」）。clear の判定は Q1）。
- 受け入れ（design §8）: color bar の目視、framebuffer の目視、`fpgareg wait` で VSYNC。
- ユーザーの方法: 「トップダウンにVerilig実装を進めてください。スタブ、ドライバの使用は恐れずに、パズルのように部分的に進めてください。」

## 置き場所と commit

- amaterrace-gpu worktree `/home/awe/amaterrace-worktrees/p3-dc`（branch `p3-dc`、base main 98885bf）: d538c63（regs・tgen・vga と TB、include の DISP の offset）、56d138b（dma と TB）、2a06338（atg_dc の結線と system の TB）、ae5622f（tmds と TB、ZYBO の pclk・tmds_out・XDC、lint の stub）、856f12e（PLL + MMCM、spec）、f756b96（tgen: TEST_PATTERN・FORMAT を vsync で取る）→ Q1 が f756b96 を main へ merge 中（2026-10-23 の Q1 の連絡）。その後 96c1619（`docs/ghci.md` §12 と Appendix）。
- zedBSD worktree `/home/awe/zedBSD-worktrees/p3-ws288-p005`（branch `agent/p3-ws288-p005`）: この phase.md、`plan/tools/zybo/ghci-dc-board.py`（板の確認の道具。Master の Tools への登録は Q1）、ws.md の p005 の行。

## 作った物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `rtl/dc/atg_dc_regs.v` | DISP の register（design §5.1）、STANDALONE の GHCI の header（MAGIC・"AMAT"・"ATDC"・ISA 0・GCLK_HZ・CAPS = [3] だけ・EXTCAP 0x100 → DISP の entry が 0x200）。CTRL.RESET は 32 cycle の pulse（pclk の側にも届ける）、FB_NEXT は ENABLE 中は vsync で flip（FLIP_DONE）、止まっている時は即時。0x70 SCAN_SUM・0x74 SCAN_WORDS（Amaterrace の debug） |
| `rtl/dc/atg_dc_dma.v` | HP1 の読み: 1 行を ceil(行の byte / 32) line、line FIFO 8 段（am_fifo）に空きがある時だけ要求（応答に back-pressure が無いため）、unpacker が 1 pixel/cycle。frame_start と ENABLE の立ち下がりで中断: tag の epoch で古い応答を捨てる。frame の pixel の和と数 |
| `rtl/dc/atg_dc_tgen.v` | h/v の counter、DE・HSYNC・VSYNC（SYNC_POL）、止まっている時は vsync の行の先頭で待つ（frame は必ず vsync から）、vsync の行で FIFO を空にする（遅れた frame の残りを捨てて次の frame を揃える）、frame_start は back porch の先頭、BGRA8/RGBA8/RGB565、TEST_PATTERN（8 本の bar: 白・黄・シアン・緑・マゼンタ・赤・青・黒、1 pixel の白枠）、underflow |
| `rtl/dc/atg_dc_vga.v` | 5-6-5 の上位 bit、DE の外は 0、1 段の register |
| `rtl/dc/atg_dc_tmds.v` | DVI 1.0 の TMDS（q_m、running disparity、control symbol。ch0 に {vs, hs}）、2 cycle |
| `rtl/dc/atg_dc.v` | 結線。CTRL.RESET を am_sync で pclk へ（am_afifo の両側を一緒に reset する規則） |
| `boards/zybo/lib/atg_zybo_pclk.v` | **PLLE2_BASE（640×480: D 3、M 34、O0 45 → 25.185 MHz、O1 9 → 125.93）+ MMCME2_BASE（720p: D 8、M 59.375、O0 10 → 74.219 MHz、O1 2 → 371.09）**、BUFGMUX 2 つ、prst_n。design の「1 つの MMCM の 4 出力」は成り立たない（共通の VCO が 1200 MHz を大きく超える）。2 つの MMCM 案は Vivado の placer が拒否（7010 の 2 つの MMCM は上下の別の半分にあり、BUFGMUX が両方の半分から dedicated route で取れない: `Place 30-99`）。それで同じ CMT の PLL と MMCM にした（Q1 ACK 2026-10-23） |
| `boards/zybo/lib/atg_zybo_tmds_out.v` | OSERDESE2 の MASTER/SLAVE（10:1 DDR）× 4、OBUFDS。clock channel 1111100000 |
| `boards/zybo/atg_zybo_dc.v`・`.xdc` | `hdmi_out_en`（F17 = 1、HDMI の口を source に。Q1 ACK）を追加。XDC: LED・VGA・HDMI・HDMI_OUT_EN の pin、gclk と 2 つの pixel clock の組は asynchronous |
| `sim/lint/xilinx_stubs.v` | PLLE2_BASE・MMCME2_BASE・BUFGMUX・OSERDESE2・OBUFDS の lint の stand-in |
| `include/ghci_regs.vh`・`.h` | DISP の offset と bit |
| `sim/tb/atg_dc_{regs,tgen,vga,dma,tmds}_tb.v`・`atg_dc_tb.v` | 各 module の TB（spec §8 の PASS 名）と DC 全体の TB（gclk 100 MHz・pclk 25 MHz の 2 clock、frame を framebuffer と比べる、flip、RGB565、RESET） |
| `spec/dc/*.md`・`spec/boards/zybo/*.md`・`lib/*.md` | 振る舞い、port の追加（regs・dma の scan_sum/scan_words、top の hdmi_out_en）、§10 Status |
| `docs/ghci.md` | §12 に 0x70〜0x7C の vendor、ENABLE = 0 の時の約束、Appendix に p005 |

Values referenced: Digilent digilent-xdc `Zybo-Master.xdc`（MIT）commit c8644a9fe31cc803789643124545d56b3bef4600（plio.xdc と同じ版。VGA: R1..R5 = M19 L20 J20 G20 F19、G0..G5 = H18 N20 L19 J19 H20 F20、B1..B5 = P20 M20 K19 J18 G19、HS P19、VS R19。HDMI: CLK H16/H17、D0 D19/D20、D1 C20/B20、D2 B19/A20、TMDS_33、HDMI_OUT_EN F17）。VESA DMT 640×480@60（H 640/16/96/48、V 480/10/2/33、負）、CEA-861 720p（H 1280/110/40/220、V 720/5/5/20、正）。UG472（MMCM・PLL の VCO・PFD の範囲、手で計算）、UG471（OSERDESE2 の 10:1）。DS187 は未読（720p の 742 Mb/s は Vivado の timing は通ったが、規格の上限は未確認）。

## 確認（host。QEMU は使わない）

| 確認 | コマンド | 結果 |
| --- | --- | --- |
| lint | `tools/lint.sh`（Verilator 5.032） | 97 件すべて clean（ae5622f の後。atg_zybo_dc の board top を含む） |
| unit TB | `sim/run.sh atg_dc_regs atg_dc_tgen atg_dc_vga atg_dc_dma atg_dc_tmds atg_dc`（iverilog） | 6 本 PASS。`SEED=2/3/42` で atg_dc_dma PASS。TMDS の running disparity は ±8 以内 |
| TB の感度（mutation） | dma の epoch の判定を外す / 行末の line の pop を外す / tgen の変換に 1 bit の xor / flip で FB_BASE を更新しない | それぞれ atg_dc_dma・atg_dc_dma・atg_dc・atg_dc が FAIL（戻して PASS） |

## Vivado（ZYBO DC、`tools/vivado-build.sh zybo atg_zybo_dc`、Vivado 2026.1）

- f756b96: LUT 1,984（11.3%）、FF 1,875、BRAM 0、IOB 31、BUFGCTRL 3。WNS **+1.401 ns**、WHS +0.019 ns。critical 0、warning 205（PS7 の未接続、意図して使わない timing・rgb の bit、hdmi_out_en の定数）。clock: gclk 100、a_px 25.185、a_px5 125.926、b_px 74.219、b_px5 371.094 MHz。bitstream sha256 先頭 e7d74902a54df0d4（`build/` は git に入れない）。
- 板の塗りに使った 2D: main と同じ rtl の `atg_zybo_2d`（WNS +0.570 ns、sha256 先頭 fd8e82c5e32db1e1）。

## 板の証拠（実機の ZYBO、SD 起動、10.0.30.2、fpgareg だけ。QEMU は使っていない。目では見ていない）

`plan/tools/zybo/ghci-dc-board.py --dc-bit … --blt-bit …`（約 2 分）: 2D の bitstream で予約の memory（0x1C000000〜）に BLT の FILL で 3 枚の framebuffer を塗り（640×480 BGRA8 @ +0x100000、1280×720 BGRA8 @ +0x400000、640×480 RGB565 @ +0x900000。bar 8 本・中央の箱・白枠、39 回の FILL）、PL を DC の bitstream に入れ替え（DDR はそのまま）、DC だけで確かめる。2026-10-23〜24 に 2 回（f756b96 の bitstream）、**2 回とも 28/28 PASS**（1 回目の前の試行で flip の FLIP_DONE だけ FAIL: 道具の `fpgareg wait --ack` が W1C で FLIP_DONE を消していた。道具を直した。RTL の誤りではない: FB_BASE と SCAN_SUM はその時も正しかった）。

| check | 結果 |
| --- | --- |
| 識別（MAGIC、VERSION 1.0、"AMAT"、"ATDC"、ISA 0、GCLK_HZ 100 MHz、CAPS 0x8、EXTCAP 0x100 → {0x101, 0x200}、DC_ID、DC_VERSION 0.1、DC_CAPS 0x473）、RESET で FRAME_COUNT 0 | PASS |
| 640×480: FRAME_COUNT の速さ（host の時計で 10 s） | 59.92〜60.02 Hz（期待 59.965） |
| 1280×720 | 59.92〜59.99 Hz（期待 59.975） |
| VSYNC の割り込み: `fpgareg wait --ack 0x218` × 20 | 各 mode で取り逃し 0（IRQ_F2P[0] → GIC 61 → `/dev/fpga0`） |
| underflow 無し（STATUS[1]・IRQ_STATUS[1]）、STATUS.running、line が動く | 3 mode とも PASS（720p の 1 frame 3.6 MB を HP1 で読んで underflow 無し） |
| SCAN_WORDS / SCAN_SUM = host の model の塗った framebuffer の和 | 640×480 BGRA8: 307200 / 0x7c1eaf78、720p: 921600 / 0x42f27538、RGB565: 307200 / 0xddf4efc4。すべて一致 = DMA が framebuffer を全部、順に読んだ |
| flip（FB_NEXT → vsync で FB_BASE、FLIP_DONE、新しい framebuffer の SCAN_SUM） | 640×480: IRQ 0x5・0x1c400000・0xd99e14bc（model と一致）。720p: IRQ 0x5・FB_BASE 一致（flip 先は塗っていない所を含むので和は比べない） |
| TEST_PATTERN で underflow 無し、ENABLE 0 → STATUS.idle（0x…4） | PASS |
| 最後の状態 | 640×480 の TEST_PATTERN を出したまま（`--leave pattern`） |

途中で板で見つけて直した物: 走っている時に CTRL = 0 を書くと TEST_PATTERN が ENABLE より 2 cycle 早く pclk に届き、その間に FIFO の空の pixel を取って UNDERFLOW が立った（720p への切り替えの時の IRQ_STATUS 0x3）。tgen が TEST_PATTERN と FORMAT を止まっている時と vsync でだけ取るようにした（f756b96）。

## ユーザーの目視の手順（Q1 がユーザーの試験の一覧に載せる。急がない）

準備: ZYBO（10.0.30.2、SD 起動の zedBSD）の **VGA の口**に VGA の monitor、**HDMI の口**に HDMI の monitor（片方だけでもよい。VGA と HDMI は同時に同じ画を出す）。host で（amaterrace-gpu の main を f756b96 以降にして）bitstream を作る:

```
cd /home/awe/amaterrace-gpu && tools/vivado-build.sh zybo atg_zybo_dc && tools/vivado-build.sh zybo atg_zybo_2d
cd <zedBSD の checkout> && B=/home/awe/amaterrace-gpu/build/vivado
python3 -I plan/tools/zybo/ghci-dc-board.py --dc-bit $B/atg_zybo_dc/atg_zybo_dc.bit --blt-bit $B/atg_zybo_2d/atg_zybo_2d.bit --leave LEAVE
```

（P3 の worktree の bitstream なら `B=/home/awe/amaterrace-worktrees/p3-dc/build/vivado`。どちらも約 2 分で、最後に `RESULT PASS` と表示し、画面を LEAVE の状態で残す。2 回目からは `--blt-bit` を省いてもよい（塗った memory が残っている限り）。）

| # | LEAVE | 見る物（期待） |
| --- | --- | --- |
| V1 | `pattern` | 640×480 60 Hz。左から白・黄・シアン・緑・マゼンタ・赤・青・黒の 8 本の縦の bar（各 80 pixel）、画面の外周に 1 pixel の白枠（右端の黒い bar の上でも見える）。monitor が 640×480 と認識し、画面が左右・上下にずれていない（25.185 MHz は VESA の 25.175 から +0.04%） |
| V2 | `fb` | 640×480 の DDR の framebuffer: 8 本の縦の bar（左の青紫から右の橙へ段々に変わる）、中央に画面の半分の緑の箱、外周に 1 pixel の白枠。上下左右の端が欠けていない、横の縞・ちらつき・ずれが無い |
| V3 | `pattern720` | 1280×720 60 Hz で V1 と同じ 8 本の bar（各 160 pixel）と白枠。HDMI で monitor が 720p と認識するか（742 Mb/s の TMDS、DS187 未確認） |
| V4 | `fb720` | 1280×720 の framebuffer: V2 と同じ形（bar・緑の箱・白枠） |
| V5 | `off` | 画面が消える（monitor が信号無しになる） |

VGA と HDMI の別に、各 # で「映った / 映らない / 色やずれの様子」を記録する。

## 未実施・制限・残り

- 目視（上の V1〜V5）は未実施。VGA の色の並び・pin の対応、HDMI の像は目視でしか確かめられない（TMDS の符号は TB だけ: decode が一致、disparity ±8）。
- HDMI の 720p（371 MHz の pclk5、742.5 Mb/s）は Vivado の timing が通った（WNS +1.401 ns）だけ。DS187 は未読。
- CTRL.OUTPUT（VGA/HDMI）は何も切り替えない（両方に出す）。HDMI の HPD・CEC・DDC は使わない。
- FB_BASE と FB_STRIDE は 32 byte 揃えが前提（下位 bit は無視）。FB_SIZE は timing の active と同じ大きさが前提（小さければ underflow、大きければ vsync で捨てる）。
- 2 つの mode だけ（PCLK_SEL）。他の mode は MMCM の DRP（design §5.1、後）。
- I2C（atg_dc_i2c）は p012（KC705）。この Phase では stub のまま。
- zedBSD の driver の側（display device）は p006。
- 試験の整理: `ghci-dc-board.py` は板の回帰として Master の Tools に載せる候補（Q1）。
