<!-- awesome-plan project=zedbsd record=ws288-p024 -->

# ws288-p024 KC705 の板での確認

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p024 の行。
- 状態: in-progress（2026-10-26 KC705 の JTAG の構成で DONE が上がらない、P1 が調べる）（2026-10-24 Q1。新設: ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」、その前に「タイミングの調整はまだ行わないので、KC705 での動作確認は後にします。実装を優先し、次のフェーズへ進んでも OK です。」）
- 依存: [p012](../phase012/phase.md)（KC705 の RTL）。timing（[p025](../phase025/phase.md)）は待たない: 低い clock で確かめる。

## 方針（2026-10-25 04:28）

ユーザー「了解です。そもそもブロックごとに動かしたいクロックは違いますし、SUは特にクロックドメインを分離する必要がありますね。今回は合成できて640x480などの小さな画面で出力できたら、それでよしとします。タイミング調整のフェーズでは、ターゲットのFmax (最大動作周波数) をどうするべきか、検討が必要です。ASIC化を考えれば200MHzでも足りないわけです。」

- GPU の core は低い clock（まず 50 MHz、合わなければ合う所まで下げる）の 1 つの clock の領域で合成する。PCIe（XDMA の 125/250 MHz）・MIG（約 200 MHz）・HDMI の pixel clock とは非同期の FIFO・AXI の clock converter で分ける。最初に Vivado の out-of-context で KC705 の構成を 50 MHz で合成して WNS を見る。合わない経路は p025 への申し送りにする。
- firmware・driver の watchdog・timeout が clock の速さに依らないかを確かめる。

## 範囲

- MIG の IP の生成（`vivado-build-ip.tcl`）、KC705 の bitstream（Vivado、必要なら openXC7）。
- JTAG の bridge（`am_jtag_regs`）で register を書いて BLT の fill → `DBG` で読み戻す（`plan/tools/kc705/`）。
- HDMI（ADV7511 を I2C で設定）で DC の color bar（ユーザーの目視）。画面は 640x480 などの小さな mode でよい。
- p007（raster）・p009（shade・tex）の scene を板で流し、sim の結果と一致。

## 受け入れ

**この Phase は、合成でき、640x480 などの小さな画面に出力できればよい**（ユーザーの方針）。実機で fill → `DBG` の一致、color bar の目視、scene が sim と一致。QEMU の証拠は無い（実機だけ）。

## 記録

- 2026-10-24 Q1: 新設。p012 の part B（板）と、p007・p008・p009 で後回しにした KC705 の確認をここに集める。
- 2026-10-25 19:21 Q1: 板の要らない準備を先に（P2、q1582）: (1) MIG の core（atg_kc705_ddr3）を ip.tcl で作る（今は「p024 で」と書いて未作成）、port を読み戻して wrapper と lint の stub を合わせる。(2) gclk を板の bring-up 用に下げる: Fmax は合成の見積もりで約 23〜25 MHz なので、MMCM の gclk を 20 MHz に（Q1 の判断、ユーザーの「合成できて640x480などの小さな画面で出力できたら、それでよし」から。50 MHz は p025 の pipeline の後）、XDC の clock の制約と PCIe の user clock・MIG の ui_clk・pclk との clock group。(3) KC705 の board 全体（MIG・PCIe・DC を含む）の合成・配置配線・bitstream の生成を Vivado で（板は無しで）、使用量と 20 MHz の timing の結果。
- 2026-10-25 19:31 Q1: Vivado の実行は host で同時に 1 つ、lock は /tmp/zedbsd-vivado.lock（flock、P2 が今日から使う）。板の JTAG は /tmp/zedbsd-kc705-board.lock（別）。
- 2026-10-25 19:53 P2 → Q1（申告）: amaterrace p2-p024 6938bce（親 8a0571c）。(1) MIG: boards/kc705/atg_kc705_ddr3.prj（board store の kc705 1.7 の mig.prj から、No Buffer の sys clk・active low の reset・AXI addr 30 / ID 4 / data 512）、ip.tcl が mig_7series 4.2 を作り、全部の core を synth_ip。Vivado 2026.1 で 9 つの core を生成（ERROR・CRITICAL 無し）、各 core の _stub.v の port は sim/lint/xilinx_stubs.v と一致（script で比較）→ wrapper と stub の port の変更は不要、atg_kc705_mig.v の頭の誤記を直した。(2) gclk 20 MHz（mmcm_g の O0 50）、board top の GCLK_HZ 20000000、XDC の clock group（MIG の ui_clk・PCIe の axi_aclk_out）、sim の model は 100 MHz のまま。(3) tools/vivado-build.tcl が IP の .xci を読む、clock の相互の report、vivado-build.sh が /tmp/zedbsd-vivado.lock。board top の lint clean。board 全体の合成・配置配線・bitstream を実行中（timeout 7000 s）。
- 2026-10-25 20:21 P2 → Q1（申告）: board 全体の Vivado の 1 回目は配置で止まった（bank 34 の DCI に bank 33 の DCI_CASCADE {32 34} が要る、CPU_RESET の AB7 が bank 34 の VR の pin）→ XDC を直し、BSCANE2 の TCK の pin に create_clock。2 回目を実行中。p2-p024 に 0b5e89e を merge = 8bfdb71。
- 2026-10-25 20:34 ユーザー「全部置き換えてからでいいと思います」（原文は Master の decisions-log）→ board 全体の合成・配置配線・bitstream の本番は p037 の後。今の 2 回目は流れの確認として終え、結果（制約・DRC・20 MHz の timing）を記録して止める。P2 は p037 の CP・MC（p039 が main に入ったので始められる）に移る。
- 2026-10-25 20:35 ユーザー「20MHzも低すぎます。その段階でP&Rをやっても、あとでひっくり返るので、無駄になりそうです」→ P2 の board の Vivado の 2 回目を止める、board の P&R は回さない、gclk 20 MHz（Q1 の判断）は取り消し（MMCM の設定は parameter として残す）。
- 2026-10-25 20:36 ユーザーの順（Master の decisions-log）: board の P&R は p037 と p025 の後。
- 2026-10-25 20:40 P2 → Q1（申告）: amaterrace p2-p024 8040e3d。2 回目は Stop の前に終わっていた: 合成（13 分）・opt の後、place_design で ERROR Place 30-487（入らない）: LUT 215,131（組み合わせ）/ 247,510（全部）に対し 203,800、slice 38,562 が要り空きは 36,821、FF 120,230 / 407,600、control set 2,930。合成の cell（GPU の RTL だけ）: LUT6 75.9k・LUT5 33.0k・LUT4 29.1k・LUT3 26.7k・LUT2 20.4k・CARRY4 9.9k・FDRE 85.3k・DSP48E1 183・RAMB36 127・RAMB18 19。修正の後の CRITICAL WARNING は 0。timing・DRC・bitstream は無し。gclk は parameter（atg_kc705_clk の GCLK_DIVIDE、gclk = 1000 MHz / N、board top の 50 は仮）。Q1 の見方: board top は SU_PK_SHARED を渡していない（既定 0 = fp16 を 2 つ同時、p036 の OOC の 167k は PK_SHARED 1）、p038 の pipeline で FF が増え、MIG・PCIe・DC が加わった → 1 SU で約 105%。
- 2026-10-25 20:39 P2（q1582、板の要らない準備の結果。amaterrace-gpu p2-p024 6938bce → 8bfdb71（atg_top_disp_tb の直し 0b5e89e の merge）→ 8040e3d）:
  - (1) MIG: boards/kc705/atg_kc705_ddr3.prj（Vivado 2026.1 の board store の kc705 1.7 の mig.prj に、ModuleName、SystemClock No Buffer、SysResetPolarity ACTIVE LOW、AXI の address 30・ID 4（data 512 は preset のまま）、UIExtraClocks 0）。boards/kc705/ip.tcl が mig_7series 4.2 を XML_INPUT_FILE（mig_a.prj）で作る。全部の core に synth_ip（out of context）、引数で一部だけも作れる。9 個の core を Vivado で生成（ERROR・CRITICAL 無し、synth_ip 込みで MIG は約 3.5 分）。各 core の _stub.v の port と sim/lint/xilinx_stubs.v を script で比べて全部一致（MIG は extra clock を 0 にして一致）→ wrapper と stub は port の変更なし。atg_kc705_mig.v の header（存在しない atg_kc705_axi_ic を名乗っていた）と spec を直した。
  - (2) gclk: atg_kc705_clk に parameter GCLK_DIVIDE（gclk = 1000 MHz / GCLK_DIVIDE、MMCM の O0）、board top の localparam GCLK_DIVIDE 50（20 MHz、仮の値: 目標ではない、ユーザーの判断で p025 で決める）と GCLK_HZ = 10^9 / GCLK_DIVIDE を atg_top へ。sim の model は 100 MHz のまま（parameter は受けるだけ）。XDC: clock group に MIG の ui_clk（mig/ddr3/ui_clk）と PCIe の axi_aclk_out（pcie/bridge/axi_aclk_out）を足し、tck の create_clock を BSCANE2 の TCK の pin に（net では synth が CRITICAL を出していた）、DCI_CASCADE {32 34} を bank 33 に（下）。tools/vivado-build.tcl は VIVADO_IP の */*.xci を読む（set_part、clock_interaction の report）、vivado-build.sh は flock /tmp/zedbsd-vivado.lock を取る。
  - (3) 板全体の Vivado（板無し、NSU 1）: 1 回目は placement で停止: DDR3 の bank 34 の DCI に VR pin が要るが AB7 は CPU_RESET（ERROR Place 30-110）→ XDC に DCI_CASCADE {32 34} [get_iobanks 33]（KC705 の MIG の通例、UG810・回路図は未確認）。2 回目は synth 13 分・opt 2.6 分・place 10 分で **入らない**: ERROR Place 30-487「50950 slice のうち 36821 が使えるが 38562 が要る」、LUT 215,131（combined）/ 247,510（total）に対して 203,800、FF 120,230 / 407,600、control set 2,930。synth の cell（GPU の RTL、IP の OOC は別）: LUT6 75,880・LUT5 32,956・LUT4 29,104・LUT3 26,665・LUT2 20,432・LUT1 4,755、CARRY4 9,869、MUXF7 5,649、FDRE 85,316、DSP48E1 183、RAMB36 127・RAMB18 19、LUTRAM（RAM32M 905・RAM64M 789 など）。CRITICAL WARNING は 0（tck の直しの後）。timing・DRC・bitstream は placement で止まったので無い。
  - ユーザーの判断（Q1 経由、2026-10-25）:「全部置き換えてからでいいと思います」（板の合成は p037 の算術の置き換えの後）、「20MHzも低すぎます。その段階でP&Rをやっても、あとでひっくり返るので、無駄になりそうです」→ 板の P&R はもう流さない。MIG・ip.tcl・XDC は commit 済み、gclk は parameter のまま。
  - 申し送り: 1 SU の KC705 の形で LUT が 105 %（combined）: 面積の削減（p036・p037 の続き、SU・TU の規模）か NSU・機能の選択が要る。生成物は amaterrace の worktree p2-p024 の build/vivado/（ip-all/ip が core、atg_kc705_raster/vivado.log）。

- 2026-10-26 04:29 ユーザー「うーん、3だけ入れて、PCIeで実機検証します。そのあとで最適化しましょう。」（Q1 の U106 の推し 1〜4: 1 MS4=1、2 PACKED 0・SUBGROUP 0、3 SU_NPHYS 8 → 4、4 BLT の RESOLVE を shader に）→ U106: KC705 の板は **3 の SU_NPHYS 4 だけ**（MS4・PACKED・SUBGROUP は今の板の top のまま、RESOLVE は変えない）。順を変える: NPHYS 4 の対応の確認と板の top → KC705 の board の P&R・bitstream → p024（板）・p013（PCIe）→ p028（VFIO の guest の Vulkan）の実機の検証 → その後に最適化（p025 の T2・T3、p041 の CDC、p032、p033）。2026-10-25 20:36 の順（p037 → p025 → p032 → p033 → board）のうち、p025 以降の最適化を実機の検証の後に回す。板の clock は配線の結果の Fmax に合わせる（p025 の前なので低い）。
- 2026-10-26 06:0x P1（q1586）途中: amaterrace p1-n4 0dd9f72（親 8e529b3）= SU_NPHYS 4 の対応と板の top を NPHYS 4 に。RTL の直し 2 つ: (1) atg_su_wb の VF・RX の入力の FIFO が 16 word 固定で、G 8（NPHYS 4）で RX の書き込みが溢れて消えた（atg_top_shade で 18 の書き込みが消え 7 pixel が違う）→ 深さ 2*(SHORT_LAT+2G+2) を 2 の冪に（NPHYS 4 で 64、他は 32）、parameter SHORT_LAT を追加、sim で消えた書き込みは ERROR。NPHYS 8 でも旧 RTL は shade の scene で RX の書き込みを 2 つ失っていた（絵は一致していた潜在の不具合）。(2) atg_su_tex の tag の quad の番号が NPHYS 4 で grp*2（tag だけ）。TB: atg_su_lane_tb の subgroup の時間の model が G <= SHORT_LAT を仮定していた。NPHYS 4 の形を atg_su・gfx・lane・fpu・pk・opd・rf・tex・lda の TB、atg_top_shade・tex・verilator・lint（NPHYS 8 も）・arith-lint・vivado-ooc に。確認（P1）: lint 196 clean、arith-lint 0、上の sim と atg_top_*・atg_kc705_raster・atg_kc705_pcie、Verilator basic・shade・tex・compute が PASS。板の build 1（GCLK_DIVIDE 50）が実行中。

### 板の準備: SU_NPHYS 4 と板の全体の build（2026-10-26 P1、q1586-i01）

U106 のユーザーの判断（2026-10-26 04:29「うーん、3だけ入れて、PCIeで実機検証します。そのあとで最適化しましょう。」）の 3 だけ: KC705 の板の SU を NPHYS 4（GRP_W 3）に。MS4・PACKED・SUBGROUP は板の top のまま（MS4 0、PACKED 1、SUBGROUP 1、PK_SHARED 0）、BLT の RESOLVE は変えない、pipeline は入れない。

- 基点: amaterrace-gpu 8e529b3、worktree `~/amaterrace-worktrees/p1-n4`（branch p1-n4）。commit **0dd9f72**（親 8e529b3、`git cat-file -t` = commit）。zedBSD の worktree `~/zedBSD-worktrees/p1-n4`（agent/p1-n4、main 74ee27f8f から）。
- **NPHYS 4 で直した RTL（2 つ）**:
  1. `atg_su_wb`: VF・RX の入力の FIFO が固定の 16 語で、NPHYS 4（G 8）では wb_stall の後も短い結果が write port を長く占め、RX の書き込みが溢れて**失われた**（atg_top_shade の NPHYS 4: 18 語が消え、7 pixel の色が違った。gdb でなく TB の hierarchical な監視で FIFO の満杯の書き込みを数えて確かめた）。深さを 2 ×（SHORT_LAT + 2G + 2）の 2 の冪に（NPHYS 4: 64、8・16・32: 32）、parameter `SHORT_LAT` を atg_su_wb に足し（spec も）、満杯の書き込みは sim で ERROR を出す。**NPHYS 8 でも潜在**: 前の RTL の atg_top_shade（NPHYS 8）で RX の 2 語が失われていた（その scene の画は一致していた）。
  2. `atg_su_tex`: tag の quad の番号が NPHYS 4 で grp × 2（QW が 1 bit のため）。TU は tag を通すだけなので、違っていたのは tag だけ。
- TB: NPHYS 4 の形を atg_su（`PASS atg_su.nphys4`、+quick にも）、atg_su_gfx（SB 8・FRAG_RESERVE 2）、lane（sub の 4 と qd の NPHYS 4 の unit）、fpu・pk（NPHYS 4 の unit が NPHYS 8 の下位 4 lane と毎 cycle 一致、`PASS atg_su_pk.nphys4`）、opd、rf（NREGS 64 も）、tex、lda に足した。atg_su_lane_tb の subgroup の時刻の模型は G ≤ SHORT_LAT を前提にしていた（期待値を x_ready の後に置き、G 8 では最初の結果に間に合わない）ので直した。tex_tb の group の配列 2048 → 4096。
- KC705 の形の写しを NPHYS 4 に: 板の top（`.SU_NPHYS(4)`・`.SU_GRP_W(3)`）、atg_top_shade・atg_top_tex（板の形と書いてある TB）、Verilator の run.sh・link-build.sh、lint.sh・arith-lint.py（NPHYS 8 の形も 1 本残した）、vivado-ooc.sh（T0 は NPHYS 8 で測ったと注記）。cs・msaa・core の atg_top TB は NPHYS 8 のまま（top での NPHYS 8 の確認）。parameter の説明と spec を「4, 8, 16 or 32」に。
- 確認（全部 PASS、2026-10-26）: `tools/lint.sh` 196 本 clean、`tools/arith-lint.py` 0。`sim/run.sh`: atg_su（全部、nphys4/8/16/32・nosubgroup、634 s）、atg_su +quick、atg_su_gfx、lane、fpu、pk、opd、rf、tex、lda、wb、atg_top_shade、atg_top_tex、atg_top_cs、atg_top_msaa、atg_top_core、atg_top_raster、atg_top_fw、atg_top、**atg_kc705_raster、atg_kc705_pcie**。Verilator の harness: basic・shade・tex・compute（NPHYS 4）。link-build.sh は未実行。

#### 板の全体の build（Vivado 2026.1、xc7k325tffg900-2）

- `VIVADO_IP=build/ip-copy/ip`（p2-p024 の `build/vivado/ip-all/ip` の写し、9 core。boards/kc705 の ip.tcl・prj は 8040e3d と 8e529b3 で差が無い）、`VIVADO_TIMEOUT=7000`。合成・配置・phys_opt・配線・bitstream まで rc 0、2,103 s、CRITICAL WARNING 0、WARNING 393。DRC は Warning と Advisory だけ（CFGBVS-1 1、DPIP-1 209、DPOP 219、PDCN-1569 6、REQP-1709 1、REQP-1839 8、RTSTAT-10 1）。
- **使用量（配線後）**: Slice LUTs **167,991（82.43%）**（logic 155,829、memory 12,162）、FF 115,255（28.28%）、Block RAM Tile 147.5（33.15%: RAMB36 137、RAMB18 21）、DSP 168（20.00%）、IOB 149、BUFGCTRL 13。階層: gpu（atg_top）131,267、SU 39,134（T0 の NPHYS 8・MS4 1 の 59,321 から）。
- **timing（配線後、全部 met、「All user specified timing constraints are met.」）**: 全体 WNS +0.177 ns（MIG の mem_refclk）、WHS +0.033 ns。clock ごと（WNS / WHS ns）:

| clock | 何 | WNS | WHS |
| --- | --- | --- | --- |
| g_out | gclk（50 ns、GCLK_DIVIDE 50 = 20 MHz） | **+3.604** | +0.051 |
| clk_pll_i | MIG の ui_clk | +0.408 | +0.054 |
| userclk2 | PCIe の user clock（axi_aclk_out） | +0.506 | +0.054 |
| userclk1・clk_125mhz・clk_250mhz_mux | PCIe の PIPE | +1.772・+3.062・+0.380 | +0.033・+0.106・+0.056 |
| a_px・b_px | pclk（640x480・720p） | +32.045・+5.791 | +0.106・+0.106 |
| mem_refclk・oserdes・iserdes | MIG の PHY | +0.177〜 | +0.061〜 |
| sysclk・tck・pcie_refclk | | +2.858・+28.174・+9.029 | +0.108・+0.111・+0.172 |

- **gclk の配線後の Fmax**: 最悪の経路は opd（`opd/b_warp_reg[0]_rep`）→ FPU（`fpu/pipe_reg[3]`）、data path 46.330 ns（logic 9.356・配線 36.974、105 段）→ Fmax 約 **21.6 MHz**（1 / (50 − 3.604) ns）。20 MHz をはっきり上回らない（GCLK_DIVIDE 47 = 21.3 MHz でも余裕は約 0.6 ns）ので、指示どおり **GCLK_DIVIDE は 50（20 MHz）のまま**、2 回目の build はしていない。
- GCLK_HZ: board top で `GCLK_HZ = 10^9 / GCLK_DIVIDE` = 20,000,000 を atg_top → atg_cp_regs の GCLK_HZ register へ（変わらず追従する）。firmware（fw/cp）に clock の速さに依る timeout は無い（grep）。watchdog は cycle で数える（WDT_CYCLES の既定 2^28、20 MHz で約 13 s）。driver（zedBSD `src/drivers/gpu/ghci/ghci.c`）は GCLK_HZ を読んで `gclk_hz` に置くだけで、待ちは壁時計の ms。ADV7511 の I2C は CLK_DIV 250 固定で 20 MHz では SCL 20 kHz、1 byte 約 0.5 ms で driver の I2C_BYTE_MS 10 に収まる（計算、実機は未確認）。
- bitstream: `~/amaterrace-worktrees/p1-n4/build/vivado/atg_kc705_raster/atg_kc705_raster.bit`、11,443,820 byte、sha256 `678b9aad624345c31380af92fc00f7d3117bc9c9e3fa764ca3da8d0267846a21`（commit していない）。routed の dcp は同じ directory。
- report: [evidence/n4-board-utilization.rpt](evidence/n4-board-utilization.rpt)、[evidence/n4-board-utilization_hier.rpt](evidence/n4-board-utilization_hier.rpt)、[evidence/n4-board-timing_summary_head.txt](evidence/n4-board-timing_summary_head.txt)（timing_summary.rpt の頭: 全体・clock ごとの表、10.9 MB の全体は build の中）、[evidence/n4-board-gclk_worst_path.txt](evidence/n4-board-gclk_worst_path.txt)、[evidence/n4-board-clock_interaction.rpt](evidence/n4-board-clock_interaction.rpt)、[evidence/n4-board-drc.rpt](evidence/n4-board-drc.rpt)、[evidence/n4-board-vivado-build.out](evidence/n4-board-vivado-build.out)。
- 未実施: 板・JTAG・PCIe・HDMI の実機（後の段）、link-build.sh の build、NPHYS 4 での TB の時間の測り直し（+quick の atg_su は 196 s、host は他の run と並行）。CFGBVS-1（CFGBVS・CONFIG_VOLTAGE の未設定）は JTAG の書き込みには通常効かないが、板の前に XDC に置くかは未判断。
- 削除の依頼（Q1 の pipeline、P1 は rm していない）: `~/amaterrace-worktrees/p1-n4/build/exp/`（sim の試し）、`build/ip-copy/`（IP の写し 251 MB、板の build を繰り返すなら残す）、`build/lda_tb.bak`、scratchpad の `exp/`。bitstream と dcp は実機の段まで残す。
- 2026-10-26 06:02 ユーザー「parameterの追加はOKです。XDCは、足しておきますが、再合成はせずにJTAGで書き込んでいきます。」→ (1) atg_su_wb の新しい parameter SHORT_LAT（FIFO の深さを latency から、P1 の 0dd9f72）を承認。(2) KC705 の XDC に CFGBVS・CONFIG_VOLTAGE を足す（Q1 の記憶の値 CFGBVS VCCO・CONFIG_VOLTAGE 2.5、UG810 で要確認）が、再合成はしない。板には今の bitstream（p1-n4 0dd9f72、sha256 678b9aad…）を JTAG で書く。XDC の変更は T1-806 の後に amaterrace の main で。
- 2026-10-26 07:27 Q1: T1-806 全 PASS（Q1 が rc と PASS・FAIL・e2e の数を log で数え直した）→ amaterrace main を 0dd9f72 に fast-forward。続けて XDC に CFGBVS VCCO・CONFIG_VOLTAGE 2.5 を足した = ae22e0c（再合成はしない、ユーザー。値は UG912 の 7 series の 2.5 V の形で、KC705 の UG810・回路図とは未照合。DRC の検査だけに効く）。板の bitstream は p1-n4 0dd9f72 の物（sha256 678b9aad…）。
- 2026-10-26 07:45 ユーザー「KC705の電源を入れました。プログラムしてオーケーです」→ Q1 が JTAG で書いた（openFPGALoader v0.13.1、-c digilent --ftdi-serial 210203368704、idcode 0x3651093 xc7k325t、/tmp/zedbsd-kc705-board.lock、p1-n4 0dd9f72 の bitstream sha256 678b9aad…、rc 0）。**構成が終わらない**: STAT 0x40181d8c（MMCM lock 1・DCI match 1・INIT 1・MODE 101 JTAG・EOS 0・GTS 0・GWE 0・Release Done 0・Done 0・STARTUP の state 110 = phase 4（UG470 の gray）、書いた直後の 1 回は 011）。再度の読み出しでも変わらない。3 回書いて同じ。原因の調べは P1（新しい起動）に。
