<!-- awesome-plan project=zedbsd record=ws288-p025 -->

# ws288-p025 timing の調整 1 段目（target の Fmax と clock の領域の検討、KC705 での調整）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p025 の行、R3。
- 状態: planned（2026-10-24 Q1。新設: ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」）
- 依存（2026-10-25 01:08 ユーザー「タイミング調整とパイプライン化は、zedBSD用のGHCIドライバを書いてから、最後に取り組みます。そうすることで、タイミング調整に使える作業時間をコントロールしたいです。」）: zedBSD の GHCI の driver（p006）の後、最後。以前の依存: RTL が一通り揃った後（p009 以降。ユーザー「全体のRTLをまずは一通り揃えることを優先したいです。」）。

## 範囲

- KC705（xc7k325t）で GPU の全 pipeline を 200 MHz に。out-of-context の Vivado で WNS を測る。
- 分かっている所: ROP の blend（2026-10-24 の測定で WNS −4.207 ns、約 108 MHz。stage 3 を 2 段に分ける差分 [blend-stage-split.patch](../blend-stage-split.patch) が未適用）、ROP の wb（11k LUT）、RX・SU・TU の長い組み合わせの経路。
- p007・p009 の記録の「速さ」の残件（asm の warp の重なり、rx_frag の quad ごと約 200 cycle、TU の addr・cache・filter）は throughput の話で、p015 と分けて扱う。

## 最初に決める事（2026-10-25 04:28、ユーザー「ブロックごとに動かしたいクロックは違いますし、SUは特にクロックドメインを分離する必要がありますね。」「タイミング調整のフェーズでは、ターゲットのFmax (最大動作周波数) をどうするべきか、検討が必要です。ASIC化を考えれば200MHzでも足りないわけです。」）

- **ASIC の目標（2026-10-25 04:52 ユーザー）**: 3 nm の mobile 向けで 1.0〜1.5 GHz（3 GHz 級は pipeline の段数がまるで違うので狙わない）。
- **target の Fmax の検討**: KC705（FPGA）の値と、将来の ASIC の値（200 MHz では足りない）を分けて考え、block ごとに決める。この Phase の最初に案を作り、ユーザーの判断を受ける（200 MHz は仮の値）。
- **clock の領域の分け方**: block ごとに動かしたい clock が違う。特に SU は独立した clock の領域にする。CP・VF・RX・TU・ROP・MC・DC の分け方、境の非同期の FIFO の位置と遅れ、MC の clock。

## 3 段の進め方（2026-10-25 04:18）

ユーザー「タイミングは、3段階のphaseをまずは作成します。１段階目のphaseで調整したがmetしない場合は、無理に帳尻を合わせず、２段階目のphaseに申し送り、１段階目はclearedにします。２段階目では、タイミングが合わなかった箇所について、まとめてFable 5.1 Highのサブエージェントに、方針のアドバイスをもらって、それに従って調整します。パイプライン化についても、レイテンシが許容できるかを総合的にFableに判断してもらってください。また、そのロジックをそのまま調整するのではなく、別なアルゴリズムに置き換えたり、近似でもVulkan仕様を満たすかも、アドバイスを受けてください。さらに、１回の調整だけでなく、何回か調整を繰り返すための、いくつかのパターンの試行錯誤の指針をもらってください。２段目でも、タイミングが合わなかったものは申し送り事項にして、clearedにします。３段目は、私が手作業で介入しますので、どこのタイミングが合わなかったか、まとめておいてください。」

- 1 段目（この Phase）: 担当（Opus）が調整する。met しない経路は無理に帳尻を合わせず、[p032](../phase032/phase.md)（2 段目）への申し送りの表（経路・WNS・試した事・理由）に残し、この Phase は cleared にする。
- 2 段目 [p032](../phase032/phase.md): Fable 5.1 High の助言で調整。3 段目 [p033](../phase033/phase.md): ユーザーの手作業。

## 受け入れ

調整を行い、200 MHz で WNS ≥ 0 になった経路と、ならなかった経路の申し送りの表（経路・WNS・試した事・理由）を記録する。sim の TB は全部 PASS のまま（T に回す）。met しない経路が残っても cleared（ユーザーの 3 段の方針）。

## 記録

- 2026-10-24 Q1: 新設。p007 の記録の「200 MHz の作業は p015（R3）でまとめて」をここに移した。
- 2026-10-25 04:12 確認（ユーザー「タイミングの調整は、Vulkan 1.0の必須項目のロジック実装後、さらにドライバとlibvulkanを実装させたあとに行う方針で、変更なしです。」）: 依存は p030（Vulkan 1.0 core の必須の logic）・p006（driver）・p019（libvulkan の側の `vk/`）の後。方針は変えない。
- 2026-10-25 12:51 ユーザー「SUはパイプラインにしましょう。ということは、FPUもパイプライン化がひつようですね。」→ SU の FPU は（複数 cycle で DSP を使い回す形ではなく）pipeline にする。Q1 の確認: atg_su_fpu・atg_su_alu は今、全部の計算を pipe[0] の前の組み合わせで行い、pipe[1..5] は遅らせるだけ（SHORT_LAT 6）。計算を各段へ分ければ、latency・scoreboard（atg_su_sb の short interlock）・ISA は変えずに pipeline にできる。
- 2026-10-25 20:36 ユーザー「そうです。ちゃんと演算器を置き換え終えて、パイプライン化を終わらせてから、取り組み直しましょう」→ 順: p037（演算器の置き換え）→ **p025**（目標の Fmax を決め pipeline、p038 の申し送りの表と p036 の block ごとの最悪の経路を使う）→ KC705 の board の P&R と bitstream → p024（板）→ p028。p025 の依存（p030・p006・p019）は満たした。
- 2026-10-25 21:59 Q1: ユーザー「P2を再度作成します。依頼事項は整理して伝えます。」→ 新しい P2 に p025 の「最初に決める事」の準備（設計の文書だけ、RTL は変えない。p037 と衝突しない）を依頼。

### P2 の作業の項目（2026-10-25 21:59、q1583、設計の準備だけ）

| # | 項目 | 中身 |
| --- | --- | --- |
| 1 | 経路の棚卸し | p036 の block ごとの最悪の経路（合成と配線後）、p038 の申し送りの表（TU の addr・desc・cube・cache など）、p025 の既知（ROP の blend）を 1 つの表に。block・経路・段数・遅れ・原因の種類（1 cycle に全部を計算、fanout、配線） |
| 2 | 目標の Fmax の案 | KC705（FPGA）の block ごとの目標と、ASIC（3 nm、mobile、1.0〜1.5 GHz）での見込みを分けて。根拠（XC7K325T -2 の DSP48E1・BRAM・CARRY4 の実力、ASIC の FO4 の段数の目安）。案は複数（例: 全体 100 MHz・SU だけ 150〜200 MHz など）と推し |
| 3 | clock の領域の案 | SU を独立の clock の領域に。CP・VF・RX・TU・ROP・MC・DC の分け方、境の am_afifo・am_sync の位置と遅れ、MC と MIG の ui_clk、PCIe の user clock、pclk。KC705 の MMCM の出力の数の制約 |
| 4 | pipeline の方針の案 | 経路の種類ごと（FP の datapath、address の計算、fanout）の段の入れ方、latency が scoreboard（SHORT_LAT）・ISA・性能（warp の数で隠せるか）に与える影響、p032（Fable の助言）に渡す論点 |
| 5 | 面積の見通し | p024 の板の build で 1 SU が LUT 約 105%（215k / 203.8k）。p037 の後の見込み、pipeline で増える FF、board の SU_PK_SHARED=1、U80（PACKED）・U81（SUBGROUP）の効果、KC705 に入れる案 |

成果: plan/ws288/design.md に新しい節（p025 の設計の段、版を上げる）と、ユーザーに判断を求める U 項目（推し付き）。RTL・amaterrace の source は変えない（読むだけ）。design-reviewer で review。

- 2026-10-25 22:35 P2（q1583、zedBSD の worktree p2-p025、branch agent/p2-p025、基点 main 05cfe206c。amaterrace-gpu main e64da02 を読んだだけ、Vivado は回していない）: 項目 1〜5 を [design.md](../design.md) **§19（版 9）** に書いた（1: §19.2 の経路の表、2: §19.3 の K1〜K4 と ASIC の 2 つの推定、3: §19.4 の領域の表と sclk ↔ gclk の 14 の channel・XDC、4: §19.5 の段の入れ方・`SHORT_LAT` と bus の使用率・p032 の論点 8 つ、5: §19.6 の面積の見込み）。U100〜U109（推し付き、§19.13）。design-reviewer の 24 件（High 6・Medium 11・Low 7、Critical 0）を全部受けて直した（§19.15）。推し: SU を独立の sclk 200 MHz・他は gclk 100 MHz（K2）、ASIC は 1.0 GHz を基準・1.5 GHz を伸ばし代、class a・f の 1 本の pipeline のまま `SHORT_LAT` 約 10〜12、境は `am_afifo` の上の channel（ASYNC parameter）、面積の判断（U106）を T2・T3 の前に（今の見込み 216k〜223k は K2 の目安 173k に入らない）、CDC（T1）は別の Phase（U109）。実装の段 T0〜T5 は 72 LW（T1 を分ければ p025 は 44）。状態: 設計の段は済み、ユーザーの U の判断待ち。p025 の実装は p037 の merge と U100・U106・U109 の判断の後。
- 2026-10-25 23:46 ユーザー「U100-U109ですが、推しで良さそうです。ただ周波数は、いったんp034, p037がマージ後に、合成結果のFmaxを見て、目標を設定しましょう。」→ WS288 design §19.13 の U100〜U109 を推しで承認。ただし周波数の数値（U100 の sclk 200 MHz・gclk 100 MHz、U101 の ASIC の数値）は決めず、p034（main に入り済み）と p037 を merge した後の合成の Fmax を見てから目標を決める。clock の領域の構成（U102・U103）、pipeline の方針（U104・U108）、SU_PK_SHARED と合成だけの build（U105）、面積を先に決める（U106）、GHCI 不変（U107）、CDC は別の Phase（U109）は推しのとおり。次: p037 の T（T1-805）→ merge → KC705 の合成（P&R なし）で block ごとの Fmax と面積を測る。
- 2026-10-26 02:10 ユーザー「それで行きましょう」（Q1 の問い「目標の周波数は次のどれにしますか」の 1 = 推し: gclk 100 MHz、SU の sclk は 150 MHz から始め 200 MHz は挑戦）→ WS288 p025 の KC705 の目標: **gclk 100 MHz、sclk 150 MHz（200 MHz は stretch）**。U100 の周波数の保留（2026-10-25 23:46）を閉じた。根拠は p037 の後の合成（rebuilt、8e529b3: LUT 168,445（82.65%）・Fmax の見積もり 28.5 MHz、phase037 の末尾）。U101（ASIC の値）は版 9 の推しのまま（1.0 GHz を基準、1.5 GHz を挑戦）。次: p025 の T0（基準の測定）→ U106（面積）の判断 → T2・T3。CDC（T1）は U109 で別の Phase（p041 に新設）。

### T0 の基準の測定（2026-10-26 P1、q1585-i01）

- 基点: amaterrace-gpu 8e529b3（p037 の後の main）、worktree `~/amaterrace-worktrees/p1-p025t0`（branch p1-p025t0、RTL と source は変えていない。`git status` は空）。zedBSD の worktree `~/zedBSD-worktrees/p1-p025t0`（agent/p1-p025t0、main 66bf54a63 から）。Vivado 2026.1、xc7k325tffg900-2、flatten の既定（rebuilt = 板の build と同じ）、VIVADO_THREADS 4、最大 3 本を並行。
- 道具: `tools/vivado-ooc.sh`（そのまま）と、dcp を開いて block ごとの最悪の経路を logic と配線に分けて書く scratch の Tcl（[evidence/t0-analyze.tcl](evidence/t0-analyze.tcl)、SU の追加の経路 [evidence/t0-extra_su.tcl](evidence/t0-extra_su.tcl)、板の合成だけの [evidence/t0-board_synth.tcl](evidence/t0-board_synth.tcl)。板の Tcl は `tools/vivado-build.tcl` と同じ読み込みで、`synth_design` と `opt_design` の後に report を書き、配置配線はしない）。
- 板の top の測定用の写し（commit しない、`build/t0-board/src*/`）: `.SU_PK_SHARED(1)`（U105）と、2 本目は `.MS4(1)` も（差分は [evidence/t0-board-top-copy.diff](evidence/t0-board-top-copy.diff)）。IP は p2-p024 の `build/vivado/ip-all/ip`（boards/kc705 は 8040e3d と 8e529b3 で差が無いことを `git diff --stat` で確かめた）を自分の build に写して `read_ip` した。

| run | top・形 | 周期 | 種類 | 時間 | LUT（report） | WNS（report） |
| --- | --- | --- | --- | --- | --- | --- |
| t0-core10 | `atg_top`、OOC の KC705 の形 + `SU_PK_SHARED=1`・PROT は板の値（31・64・6・16・4）・`GCLK_HZ` 100 MHz | 10 ns | 合成、block ごと | 969 s | 168,341（82.60%）、DSP 192 | −33.005（合成の見積もり、SU の FPU） |
| t0-su667 | `atg_su`、KC705 の形（NPHYS 8、PACKED 1、PK_SHARED 1、SUBGROUP 1、MS4 1） | 6.67 ns | 合成 + 配置配線 | 1,411 s | 61,540（30.20%）、DSP 66 | 合成 −36.335、**配線 −38.840** |
| t0-su5 | 同じ | 5 ns | 合成 | 436 s | 61,850 | −38.005 |
| t0-fpu667・t0-fpu5 | `atg_su_fpu`（NPHYS 8、SBE_W 3、PACKED 1、PK_SHARED 1） | 6.67・5 ns | 合成 | 161・160 s | 20,747、DSP 32 | −33.194・−34.864 |
| t0-tu10 | `atg_tu`（LINE 64） | 10 ns | 合成 + 配置配線 | 603 s | 23,506、DSP 62 | 合成 −17.904、**配線 −15.118** |
| t0-blt10 | `atg_blt`（LINE 64、MS4 1） | 10 ns | 合成 + 配置配線 | 625 s | 16,275 | 合成 −16.435、**配線 −16.212** |
| t0-rx10 | `atg_rx`（HAS_SU 1） | 10 ns | 合成 + 配置配線 | 369 s | 15,662、DSP 10 | 合成 −5.667、**配線 −4.654** |
| t0-su-sg0・pk0・n4・n4min | `atg_su` の面積の比較（SUBGROUP 0 / PACKED 0 + PK_SHARED 0 / NPHYS 4 + GRP_W 3 / その 3 つ全部） | 6.67 ns | 合成 | 274〜400 s | 58,192 / 53,145 / 37,320 / 31,412 | −36.263 / −28.740 / −36.319 / −28.955 |
| board-pks | `atg_kc705_raster` + SU_PK_SHARED 1（MS4 は今の板の top のまま 0） | 板の XDC | 合成 + opt_design | 1,102 s | 合成 193,836（95.11%）、opt 191,991（94.21%） | —（下） |
| board-pks-ms4 | 同じ + MS4 1 | 板の XDC | 合成 + opt_design | 1,203 s | 合成 205,280（100.73%）、opt 203,436（99.82%） | — |

全部 rc 0、CRITICAL WARNING 0（vivado-ooc.sh の集計と板の log の grep）。

#### 経路の結果（[design.md §19.2](../design.md) の表を更新した）

- **SU（sclk 150 MHz の目標）**: 単体の配置配線（LUT 30%、板の混雑なし）で WNS −38.840 ns（6.67 ns）。met は dcache だけ。最悪は opd の operand の選択 → FPU（128 段、45.417 ns = logic 9.758 + 配線 35.659）で、150 MHz で 7 段・200 MHz で 10 段（推定）。次に lane（30.6 ns）、XU（26.0）、PK（24.0）、EXP（23.4、sched の `wg_x` から register の無い DSP を通って LUTRAM の address へ）。
- **SU の control**: 短い結果の書き戻しの valid（`alu/pipe_reg[5]` → net `sv`、fanout 542）が sched・dec・sb・opd・fetch・rf の CE・R に配られ、配線後 17.5〜18.0 ns（15〜18 段、配線が 94%）。段を入れるだけでは直らない（複製と decode の前倒し、§19.5.1 の 3・7）。`atg_su_sb` の issue の道（`sb/e_base` → `opd/take`、fanout 116）は 8.643 ns（10 段）、dec → sb は 10.885 ns。
- **opd の fanout 2,585**: flatten none（p037 の P2）の 1 本の net。rebuilt では Vivado が `opd/b_warp_reg` を複製し（`_rep`・`_replica_2` など）1 本あたり 125〜131。FPU の経路の内 opd の分は配線後の最初の約 6〜10 ns（lane ごとの 32 bit の `u_cl` の加算と `opl`・`fmod`）。
- **板の SU の中の LUT**（board-pks の opt の階層、rebuilt なので行は近似）: fpu 17,577、**opd 15,702**、exp 4,844、lane 3,966、lsu 3,029、alu 2,504、xu 2,105、dcache 2,030。opd の 15.7k は §19.6 に無かった大きさで、原因は未調査（3 つの operand × 8 lane の選択・`u_cl` の 24 個の 32 bit 加算・`opc` の共通部の mux）。
- **gclk の block（100 MHz の目標）**: 配線した 3 つ（単体）: TU −15.118（addr の foot 24.966 ns、67 段 → 3 段）、BLT −16.212（MS4 の favg 26.184 ns → 3 段）、RX −4.654（setup の edge 14.625 ns → 2 段）。core の合成だけの物: MC の grant → PROT の PLB（LUTRAM、fanout 194）→ 全部の client の状態が 12〜14.7 ns（28〜39 段）で、**CP・CPU・VF・DC・TU と SU の cache・ROP の ds の最悪は全部この 1 つの始点**（grant・PLB の応答を 1 段の register にすれば一緒に直る見込み）、ROP の fblend −3.513、VF の conv −2.370、DC の cursor の BRAM → pixels −2.614。
- **板の timing**: 合成だけで、gclk は p024 の低い値（`GCLK_DIVIDE` 50）なので、opt の後の WNS +0.201 ns は 100 MHz の判断に使えない（[evidence/t0-board-pks-opt_timing_head.txt](evidence/t0-board-pks-opt_timing_head.txt)）。

#### 板の全体の合成（U105、hierarchy の utilization、`opt_design` の後の Slice LUTs で揃えた: review M11）

| 構成 | 全体 | core（`gpu` = atg_top） | MIG の側（`mig`: MIG 14,908 + clock converter 1,046 + 128 → 512 の up 2,024 + BAR2 の cc 504 + xbar 719） | PCIe の側（`pcie`: axi_pcie 14,940 + host_dn 1,044 + bar0_dn 754 + host_cc 488 + split 451） | 板の glue（bar0 の CDC 551・split 612・win_gate 111・jtag 53・top 230 など） | FF | BRAM36 | DSP |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| board-pks（MS4 0 = 今の板の top） | **191,991（94.21%）** | 153,517（75.33%） | 19,202（9.42%） | 17,689（8.68%） | 1,583（0.78%） | 120,027（29.45%） | 155.5 | 192 |
| board-pks-ms4（MS4 1） | **203,436（99.82%）** | 164,962（80.94%） | 19,202 | 17,689 | 1,583 | 124,512（30.55%） | 155.5 | 192 |

- 合成の直後（opt の前）は 193,836（95.11%）と 205,280（100.73%）。core の block（MS4 1、opt、rebuilt の行なので和は ±0.3k 合わない）: SU 59,321、ROP 25,437、TU 23,313、BLT 16,031（MS4 0 では 4,638）、RX 15,615、MC 8,251、VF 4,752、DC 3,744、CP 3,507、CPU 3,320、PROT 957。report は [evidence/t0-board-pks-opt_utilization_hier.rpt](evidence/t0-board-pks-opt_utilization_hier.rpt) と [evidence/t0-board-pks-ms4-opt_utilization_hier.rpt](evidence/t0-board-pks-ms4-opt_utilization_hier.rpt)。
- **見つけた事（要判断）**: **今の板の top（`boards/kc705/atg_kc705_raster.v`）は `MS4` を渡していない**（既定 0）。OOC の形（`tools/vivado-ooc.sh`）は MS4 1。Vulkan 1.0 の Required Limits は `framebufferColorSampleCounts` に 1 と 4 を求める（design.md §15 の表の行）ので、Vulkan 1.0 の板には MS4 1 が要り、その時 **+11,445 LUT（測定、opt）**。§19.6 の p024 の 215k は MS4 0・PK_SHARED 0 の place の指標で、今回の 2 本とは比べられない（指標を揃えたのは今回の 2 本だけ）。

#### U106 の材料（面積、ユーザーの判断に）

**目安**: 85% = 173,230 LUT、90% = 183,420 LUT。今（MS4 1、PK_SHARED 1）は 203,436 で、85% まで **−30.2k**。p025 の残り（T1 の CDC +3〜4k、T2・T3 の pipeline の register と control +6〜12k、§19.6 の推定）を足すと **−39k〜−46k**（推定）。MS4 0 なら −18.8k と −28k〜−35k。

| 手 | LUT の効果 | 確度 | Vulkan・性能への影響 |
| --- | --- | --- | --- |
| U80 PACKED 1 → 0（PK_SHARED も 0） | **−8,395**（SU の OOC: 61,540 → 53,145） | 測定（SU 単体の合成） | packed fp16 の命令が無くなる（Vulkan 1.0 は求めない。`shaderFloat16` の速さの利点が無くなる）。**FPU の経路も短くなる**: 合成の WNS −36.335 → −28.740 ns（6.67 ns、+7.6 ns） |
| U81 SUBGROUP 1 → 0 | **−3,348**（→ 58,192） | 測定 | Vulkan 1.1 の subgroup の機能が無くなる（1.0 には要らない） |
| SU_NPHYS 8 → 4（GRP_W 3） | **−24,220**（→ 37,320）。PACKED 0・SUBGROUP 0 と合わせて **−30,128**（→ 31,412） | 測定（合成だけ）。**NPHYS 4 は `atg_su` の parameter の説明（8・16・32）の外で、sim で確かめていない** | Vulkan の機能は変わらない。同じ clock で SU の lane の throughput が半分: sclk 150 MHz × 4 = gclk 100 MHz × 8 の 75%、200 MHz で 100%。1 命令の bus の cycle G が 4 → 8 になるので、`SHORT_LAT` の伸びは少ない warp で隠れる（§19.5.2 の式で SHORT_LAT 12・k = 2 で 100%） |
| MS4 1 → 0 | −11,445（板、opt） | 測定 | **Vulkan 1.0 に合わない**（4x が必須）。推さない |
| BLT の RESOLVE を shader に（§19.5.4 の 6） | 約 −11k（OOC の `atg_blt_rsv` 11,887 の大半） | 推定 | Vulkan は同じ（driver・firmware の変更、resolve の速さが下がり得る）。BLT の最悪の経路（26.2 ns）も消える |
| U84 fp32 の FPU の lane を半分 | 約 −8k（板の FPU の 8 × (u_op 約 1,876 + mul 約 146) の半分） | 推定（parameter が無い、RTL の変更） | fp32 の throughput が半分 |
| opd の作り直し（15.7k） | 不明（要調査） | — | 無し |
| ROP の 8 bit と f32 の blend を 1 つ・TU の 8 bit と fp32 の filter を 1 つ・RX の pb を BRAM | 約 −5〜10k・−5〜8k・−2〜3k | 推定（§19.6 のまま、測っていない） | §19.6 のとおり（p032 の論点） |

**推し**:

1. 板の top に **MS4 1** を足す（Vulkan 1.0。今の板の top は 0 のまま）。
2. KC705 では **PACKED 0・SUBGROUP 0**（U80・U81 を KC705 だけ 0、RTL は残す）: 測定で −11.7k（2 つの run の和）、Vulkan 1.0 に影響なし、FPU の経路も短くなる。→ 約 191.7k（94.1%、推定）。
3. 残り（85% まで約 −18.5k、p025 の追加の後は約 −27〜−34k）は **NPHYS 4** を推す: parameter だけで測定で一番大きく（3 つ合わせて −30.1k → 板で約 173.3k = 85.0%、推定）、sclk を 150〜200 MHz に上げる効果で throughput は今の 75〜100%。ただし NPHYS 4 を `atg_su` の対応の範囲に入れる確認（TB の NPHYS 4 の形、`GRP_W` 3）が要る（T2 の前の小さな仕事）。NPHYS 8 は ASIC と大きな FPGA の既定のまま。
4. p025 の追加（推定 +9〜16k、NPHYS 4 なら SU の分が半分ほど）で 85% を越えたら、次に **BLT の RESOLVE を shader に**（約 −11k、推定、p032 の論点 6）。U84 は NPHYS 4 と重なるので推さない。

**sclk 150 MHz での LUT の上限**: **85% を目標のまま、90%（183,420）を上限**にする（U106 の推しの (3) と同じ数）。理由: (1) 85% は配線の混雑の目安で、clock の値にはほとんど依らない。p036 の 95% の配線では短い経路も 10〜12 ns を失い、これは 6.67 ns（sclk 150）にも 10 ns（gclk 100）にも 1 周期以上で、clock を 200 → 150 に下げても吸収できない。(2) 今回の SU 単体の配線（LUT 30%、混雑なし）でも control の経路は遅れの 90% 以上が配線（fanout の分）で、混雑が加わる板ではさらに悪くなる。(3) 90% を越える構成では、OOC の値は「構造の確かめ」とし、板の timing を閉じる作業はしない。

#### 未実施・制限

- 配線は SU・TU・BLT・RX の単体だけ（指示どおり、core 全体と板は配線していない）。単体の配線は板の混雑を含まない。ROP・VF・MC・CP の配線、SU の 5 ns の配線、flatten none の block ごとの比較は未実施。
- 単体の top（SU・FPU・TU・BLT・RX）の port からの経路は OOC で制約が無い（FPU の単体は port に 0 ns の input delay を置いた形も [evidence/t0-fpu5-io-timing_summary.rpt](evidence/t0-fpu5-io-timing_summary.rpt) に）。core の OOC の DC の pclk の側は unconstrained。
- NPHYS 4・PACKED 0・SUBGROUP 0 の面積は SU の単体の合成の差で、板での値は差を足した推定。U84・BLT の RESOLVE を shader に・opd の作り直しは測っていない。
- 板の合成は MIG・PCIe の IP を p2-p024 の生成物から写した（8040e3d の ip.tcl、8e529b3 と同じ）。板の timing は p024 の低い gclk のままで使えない。

#### 削除の依頼（Q1 の pipeline、P1 は rm していない）

- `~/amaterrace-worktrees/p1-p025t0/build/vivado/`（OOC の 13 の run、約 240 MB）と `~/amaterrace-worktrees/p1-p025t0/build/t0-board/`（IP の写し 251 MB・板の 2 本の dcp・測定用の top の写し、約 866 MB）。T2・T3 で基準に使うなら、dcp だけ残す判断は Q1。
- scratchpad の `t0/`（session の一時物）。

#### 再開点

- T0 は済み（表と report が evidence/ に）。次は **U106 の判断**（上の推し）。その後 T2（SU の pipeline）・T3（gclk の block）。T2 の最初の対象: FPU の operand の register（opd の段 B）と FRACT の直列、短い結果の valid の fanout（`sv` 542）。T3 の最初: MC の grant・PLB の応答の register（1 つで CP・CPU・VF・DC・cache が直る見込み）、TU の addr の foot・tcrd、RX の edge。
