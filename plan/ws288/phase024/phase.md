<!-- awesome-plan project=zedbsd record=ws288-p024 -->

# ws288-p024 KC705 の板での確認

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p024 の行。
- 状態: planned（2026-10-24 Q1。新設: ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」、その前に「タイミングの調整はまだ行わないので、KC705 での動作確認は後にします。実装を優先し、次のフェーズへ進んでも OK です。」）
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
