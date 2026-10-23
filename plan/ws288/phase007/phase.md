<!-- awesome-plan project=zedbsd record=ws288-p007 -->

# WS288 p007 RX + ROP（bitstream 3「raster」、DRAW_RAW、DEBUG_COLOR、ROP の順序）

- 親: [ws.md](../ws.md)。設計: [design.md](../design.md) §2.3、§2.4.3（DRAW_RAW）、§3.6、§3.8、§8 の p007 の行。
- 担当: P3（2026-10-24、Q1 の依頼）。状態: in-progress（2026-10-24 ラップアップ: unit の RTL と TB は全部 PASS、atg_top の raster の TB・Vivado・板は未着手。clear の判定は Q1）。
- ユーザーの方法: 「トップダウンにVerilig実装を進めてください。スタブ、ドライバの使用は恐れずに、パズルのように部分的に進めてください。」

## 置き場所と commit

- amaterrace-gpu worktree `/home/awe/amaterrace-worktrees/p3-rx`（branch `p3-rx`、base main 1c79d02）: 36d1a5f、03e4016、f16a007、…、f518b2a、**8106fc3**（最後。push していない）。
- zedBSD worktree `/home/awe/zedBSD-worktrees/p3-ws288-p007`（branch `agent/p3-ws288-p007`）: この phase.md だけ。`plan/tools/zybo/ghci-raster-board.py` は未作成。

## 作った物（amaterrace-gpu）

| path | 中身 |
| --- | --- |
| `tools/rxmodel.py` | 参照 model（RTL からではなく spec の式から書いた）: fp32（RNE、指数は無制限で丸めてから denormal を 0、正準 NaN）、clip、setup、scan（top-left、整数の edge）、ds、blend、wb。`python3 -I tools/rxmodel.py` で自己検査（numpy と照合） |
| `tools/rx-vectors.py` | model から `sim/vec/*.txt|hex` を作る（git に入れた）。共有辺が画素中心を通る四角形で「各画素 1 回」を assert |
| `tools/rx-ucode.py` | `include/atg_rx_setup_uc.vh`（setup の micro-program、106 命令）と `include/atg_rop_unorm8.vh`（c/255 の表） |
| `rtl/gpu/rx/atg_rx_fadd.v`・`fmul.v`（4 段の pipeline）・`fdiv.v`（1 bit/cycle） | **新しい module**（spec は未作成: 残り） |
| `rtl/gpu/rx/atg_rx_clip.v` | DRAW_RAW（STATE 0x1E0〜0x1E8）、PT から 12 word、近平面 z ≥ 0 で 0〜2 三角形、重み |
| `rtl/gpu/rx/atg_rx_setup.v` | micro-program の fp と整数の段: 透視除算・viewport、15.8 の固定小数、D、cull・facing、bbox ∩ scissor ∩ RT_SIZE、edge、z・1/w・b0/w・b1/w の plane（raw は z だけ） |
| `rtl/gpu/rx/atg_rx_scan.v` | direct の道（quad を 1 cycle/quad で歩き、覆われた quad の z に 20 cycle、三角形に 1 つの連番、最後の quad に dx_last、空なら mask 0 の 1 quad）、pt_free。**warp の道は p009**（HAS_SU 1 も当面 direct） |
| `rtl/gpu/rx/atg_rx_order.v`・`rtl/gpu/rop/atg_rop_order.v` | 連番、順序の退役（ex_next）と busy |
| `rtl/gpu/rop/atg_rop_ds.v` | D32F・D24S8 の 8 関数、書き込み、DEBUG_COLOR の展開。**stencil の op は未実装** |
| `rtl/gpu/rop/atg_rop_blend.v` | 3 段、4 画素/cycle、unorm8 で blend（Vulkan の factor 0〜14、op 0〜4）、WRITE_MASK、BGRA8/RGBA8/RGB565 |
| `rtl/gpu/rop/atg_rop_wb.v` | 行ごとの 2 line、miss で dirty byte だけ書き戻し、必要な時だけ読み、16 cycle の idle で書いて空にする（busy はそれまで） |
| `rtl/gpu/cp/atg_cp_seq.v` | SET_REG が RX の範囲（0x050〜0x06F）は RX か ROP、ROP の範囲（0x070〜0x08F）は ROP の busy の間待つ（spec の 1 行も直した） |
| `sim/tb/atg_rx_{fp,order,setup,scan,clip}_tb.v`・`atg_rop_{order,blend,ds,wb}_tb.v` | model の vector と比べる unit TB |

## 決めた事（Q1 への spec の変更の提案。spec の本文はまだ直していない）

- `sp_tri` の edge: 各 96 bit = `{E[47:0], B[23:0], A[23:0]}`（E は bbox の原点の画素中心での値、top-left の bias 込み、subpixel² 単位）。32 bit の C では 16.8 で正確に表せないため。**guard band は ±16384 px（15.8）**（design §3.8 の 16.8 から縮めた）。z の plane の C は bbox の原点の値。bbox の max は排他的。
- 近平面は Vulkan の z ≥ 0（spec の「z > -w」は GL の式なので採らない）。DEPTH_CLAMP は見ず、z は常に [0, 1] に clamp。
- STATE の bit（ghci.md に未記載）: SCISSOR_MIN/MAX `{y, x}`（MAX 0 = 無し）、RT_SIZE `{h, w}`、CULL [1:0] 1 front・2 back・3 両方、[2] front = CCW、DEBUG_COLOR と BLEND_CONST は RGBA8（R が [7:0]）、BLEND [0] 有効・[4:1] src color・[8:5] dst color・[11:9] color op・[15:12] src α・[19:16] dst α・[22:20] α op（VkBlendFactor・VkBlendOp の番号）、WRITE_MASK [3:0] RGBA（0 は何も書かない）、DEPTH の func は VkCompareOp、format bit が無ければ test 無し。DS_BASE・RT_BASE・stride は 8 byte 揃え（565 は 4）。
- 速さは spec の目標より遅い（v1）: setup は raw で約 150 cycle、clip 空間で最大 908 cycle/三角形、ds は 1 quad/AMB 往復×行、wb は 1 quad/約 6 cycle（hit）。
- 新しい module 3 つ（fadd・fmul・fdiv）。

## 確認（host。QEMU・板は使っていない）

| 確認 | 結果 |
| --- | --- |
| `sim/run.sh atg_cp_seq atg_top atg_rx_fp atg_rx_order atg_rx_setup atg_rx_scan atg_rx_clip atg_rop_order atg_rop_blend atg_rop_ds atg_rop_wb`（8106fc3） | 11/11 PASS（fp 6000 演算、setup 160 三角形、scan 49 三角形 3879 quad、clip 60 primitive 85 三角形、blend 3000 quad、ds 38 run、wb 12 run） |
| `tools/lint.sh`（8106fc3） | 102 clean、FAILED 0 |
| 全 TB（`sim/run.sh` 引数無し）・seed 違い・mutation | **未実施** |

## 再開点と残り（次の順）

1. spec を直す: `spec/gpu/rx/atg_rx_{clip,setup,scan}.md`（sp_tri の新しい layout、guard band、近平面、速さ、scan の TB の `warps` は p009）、`spec/gpu/rop/*.md`（stencil 未実装、STATE の bit）、新しい `spec/gpu/rx/atg_rx_{fadd,fmul,fdiv}.md`、`docs/ghci.md` の STATE の bit。Q1 の ACK。
2. atg_top の raster の TB（`HAS_RX=1 HAS_ROP=1`、ring で SET_REG + DRAW_RAW を重なる三角形で、depth・blend の順、framebuffer を `rxmodel.draw_raw` と比べる）。BLIT の後は WAIT_IDLE を入れる（DRAW_RAW は RX だけ待つ）。
3. Verilator の PNG（harness に raster の mode）。
4. `boards/zybo/atg_zybo_raster.xdc`（2d の物を写す）、`tools/vivado-build.sh zybo atg_zybo_raster`、WNS を見る（setup の fp と ds/wb の乗算が怪しい）。
5. `plan/tools/zybo/ghci-raster-board.py`（ghci-blt-board.py の形、fpgareg + ring で DRAW_RAW、DBG で framebuffer を model と照合）と板の確認、DC の bitstream で目視の手順。
6. 残す物: stencil の op（KC705）、warp の道（p009）、ROP に facing が届かない（back の stencil 用、port の追加は p009 で）。

## 2026-10-24 Q1: 再開点 2（atg_top の raster の TB）— 統合の不具合を見つけた

- amaterrace-gpu の branch `p007-raster`（main に未 merge。TB が FAIL するので main の `sim/run.sh` を壊さないため）: `tools/rx-raster-scene.py`（64×48 の RGBA8 と D32F、depth LESS + write、DEBUG_COLOR の 10 三角形、後の 3 つは src-alpha の blend、最後に FENCE。期待値は rxmodel）、`sim/vec/raster_scene.txt`、`sim/tb/atg_top_raster_tb.v`（HAS_RX・HAS_ROP、host の口だけ、ZYBO と KC705 の形）。
- 結果: ring は最後の FENCE まで走る（FAULT 無し）が、**塗られた画素の depth（2,327 word）が全部 model と違う**。色の違い 340 は depth の違いによる隠れ方の差。例: 0x300c4 GPU 3f274403 / model 3f275eab、0x300d0 GPU 3f293b40 / model 3f2b947b（近いが違う）。unit の TB（setup 160・scan 3879 quad の vector）は PASS なので、top での z の受け渡し（RX の scan の quad の z → ROP の ds、lane の順・z の plane の基点・raw の時の viewport の z の扱い）のどこかの食い違いとみる（未解析）。
- 次: VCD か $display で、最初の三角形の 1 quad の z を RX の出口と ROP の入口と model で比べる。直したら branch を main に merge して sim/run.sh に入れる。

- 2026-10-24 Q1: 上の depth の食い違いの原因と修正。DRAW_RAW の 9 番目の word（3 つ目の頂点の z、STATE 0x1E8）が clip に届く前に raw_kick が出ていた（p017 の timing の直しで atg_cp が STATE の配布を 1 cycle 遅らせたのに、payload が header の後に来る op（DRAW_RAW・DISPATCH・LOAD_CONST）は最後の payload の次の cycle に kick していた）。print の probe で setup の z の plane → clip の rw[8] = x と追った。修正: `atg_cp_seq` で最後の payload を取った時に settle を 1 cycle 入れる。TB に「raw_kick の時に 9 word が全部書かれている」の assert を足した（修正を外すと FAIL する事を確かめた）。結果（amaterrace-gpu main 2d5fcf2、push 済み）: atg_top_raster は ZYBO と KC705 の形で色と depth が model と word ごとに一致、lint 102 clean、sim 60/60 PASS、Verilator PASS。
- 残りの再開点: 3）Verilator の raster の PNG、4）Vivado は KC705 で（ZYBO の raster の bitstream は作らない: ユーザーの決定）、5）板の script（KC705）。

- 2026-10-24 Q1: 再開点 3 は済み。`sim/verilator/run.sh` に raster の mode（`-GHAS_RX=1 -GHAS_ROP=1 -DRASTER`、同じ `sim/vec/raster_scene.txt` を ring で流し、色と depth の 6,145 word を model と照合、色の buffer を `build/verilator/raster.png` に。zlib 無しの PNG の writer）。PASS（75,195 cycle）。amaterrace-gpu dd09409。
- 同日（ユーザーの依頼）: amaterrace-gpu の top level の `Makefile`（`make` は help、`lint`・`sim`・`verilator`・`check`、`oxc-synth`・`oxc`（openXC7: yosys・nextpnr-himbaechel・fpga-as、chipdb は初回に作る）、`vivado`、`prog-oxc`・`prog-vivado`（openFPGALoader、`FTDI_SERIAL`）、既定は KC705 の `atg_kc705_raster`）、`tools/vivado-build.sh` は PATH の vivado を使う、KC705 の top の firmware の名前を今の image（`atg1-cp-kc705.*.hex`）に直した、GitHub Actions の CI（`.github/workflows/ci.yml`: lint・sim・Verilator（raster の PNG を artifact に）と、KC705 の yosys の合成）。KC705 の nextpnr と bitstream は XDC が無い（p012）ので CI には入れていない。CI は Ubuntu 24.04 の apt の verilator・iverilog・yosys で、host の版（Verilator 5.032）と違う。

- 2026-10-24 Q1: 再開点 4 の一部（KC705 の Vivado）。board の top は stub なので、`atg_top` を out-of-context で（xc7k325tffg900-2、LINE_BYTES 64・DATA_W 512・HAS_CPU・HAS_RX・HAS_ROP、NSU 0、clock 5 ns = 200 MHz）配置配線。LUT 40,882（20.1%）・FF 21,009・BRAM 17・DSP 32。**WNS −3.153 ns**（約 122 MHz 相当、21,504 の endpoint が違反）。module ごと: ROP 21,138（wb 11,176・blend 8,446）、RX 7,466（setup 3,044・scan 2,267・clip 2,154）、BLT 4,509、CPU 3,134、MC 2,273（tree 1,854）、CP 2,230。最も遅い path は ROP の blend（`q2_reg` → `b_pix_reg`、25〜27 段、CARRY4 14〜17 = unorm8 の乗算と /255 の丸めを 1 段で）。次: blend を段に分ける（乗算・/255・合成）、wb の大きさの理由を見る（11k）、200 MHz に届くまで。
- 2026-10-24 Q1: ROP の blend の ÷255 を幅の狭い形に（y = x + 128、下位の byte の加算からは繰り上がりだけ、上位は (y >> 8) + (y >> 16) + 繰り上がり。0〜2×255² の全部で round(x/255) と一致を確かめた。前の形は 36 bit の加算 3 項）。lint 102 clean、blend・wb・top の raster の TB PASS。amaterrace-gpu 56af5a4。Vivado の流し直しは次。ユーザーと話した事: 積の表引き（256×256、32 本で BRAM36 約 224）や項ごとの ÷255 は、規模がほぼ同じか増え、丸めが 2 回になるので採らない。足りなければ乗算と ÷255 の間に register を入れる。
- 2026-10-24 Q1: ÷255 の後に KC705 の out-of-context の Vivado を流し直した（同じ条件）: LUT 39,575（前 40,882）、blend 7,169（前 8,446）、wb 11,214。WNS −4.207 ns（約 108 MHz、前 −3.153 ns）に悪化、最悪の経路は blend の p2/q2 → b_pix（30〜32 段、CARRY4 18〜20）。stage 3 を「÷255 の入力まで」と「÷255・飽和・形式」に分ける差分（LAT 3 → 4、wb は b_rvalid を待つので影響なし）を作ったが、ユーザーの決定（「今回は合成できればいいことにして、KC705での動作とパイプライン分割はあとにしませんか？全体のRTLをまずは一通り揃えることを優先したいです。」）で適用せず、[blend-stage-split.patch](../blend-stage-split.patch) に置いた。200 MHz の作業は p015（R3）でまとめて行う。
