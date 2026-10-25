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
- 2026-10-25 20:39 P2（q1582、板の要らない準備の結果。amaterrace-gpu p2-p024 6938bce → 8bfdb71（atg_top_disp_tb の直し 0b5e89e の merge）→ 8040e3d）:
  - (1) MIG: boards/kc705/atg_kc705_ddr3.prj（Vivado 2026.1 の board store の kc705 1.7 の mig.prj に、ModuleName、SystemClock No Buffer、SysResetPolarity ACTIVE LOW、AXI の address 30・ID 4（data 512 は preset のまま）、UIExtraClocks 0）。boards/kc705/ip.tcl が mig_7series 4.2 を XML_INPUT_FILE（mig_a.prj）で作る。全部の core に synth_ip（out of context）、引数で一部だけも作れる。9 個の core を Vivado で生成（ERROR・CRITICAL 無し、synth_ip 込みで MIG は約 3.5 分）。各 core の _stub.v の port と sim/lint/xilinx_stubs.v を script で比べて全部一致（MIG は extra clock を 0 にして一致）→ wrapper と stub は port の変更なし。atg_kc705_mig.v の header（存在しない atg_kc705_axi_ic を名乗っていた）と spec を直した。
  - (2) gclk: atg_kc705_clk に parameter GCLK_DIVIDE（gclk = 1000 MHz / GCLK_DIVIDE、MMCM の O0）、board top の localparam GCLK_DIVIDE 50（20 MHz、仮の値: 目標ではない、ユーザーの判断で p025 で決める）と GCLK_HZ = 10^9 / GCLK_DIVIDE を atg_top へ。sim の model は 100 MHz のまま（parameter は受けるだけ）。XDC: clock group に MIG の ui_clk（mig/ddr3/ui_clk）と PCIe の axi_aclk_out（pcie/bridge/axi_aclk_out）を足し、tck の create_clock を BSCANE2 の TCK の pin に（net では synth が CRITICAL を出していた）、DCI_CASCADE {32 34} を bank 33 に（下）。tools/vivado-build.tcl は VIVADO_IP の */*.xci を読む（set_part、clock_interaction の report）、vivado-build.sh は flock /tmp/zedbsd-vivado.lock を取る。
  - (3) 板全体の Vivado（板無し、NSU 1）: 1 回目は placement で停止: DDR3 の bank 34 の DCI に VR pin が要るが AB7 は CPU_RESET（ERROR Place 30-110）→ XDC に DCI_CASCADE {32 34} [get_iobanks 33]（KC705 の MIG の通例、UG810・回路図は未確認）。2 回目は synth 13 分・opt 2.6 分・place 10 分で **入らない**: ERROR Place 30-487「50950 slice のうち 36821 が使えるが 38562 が要る」、LUT 215,131（combined）/ 247,510（total）に対して 203,800、FF 120,230 / 407,600、control set 2,930。synth の cell（GPU の RTL、IP の OOC は別）: LUT6 75,880・LUT5 32,956・LUT4 29,104・LUT3 26,665・LUT2 20,432・LUT1 4,755、CARRY4 9,869、MUXF7 5,649、FDRE 85,316、DSP48E1 183、RAMB36 127・RAMB18 19、LUTRAM（RAM32M 905・RAM64M 789 など）。CRITICAL WARNING は 0（tck の直しの後）。timing・DRC・bitstream は placement で止まったので無い。
  - ユーザーの判断（Q1 経由、2026-10-25）:「全部置き換えてからでいいと思います」（板の合成は p037 の算術の置き換えの後）、「20MHzも低すぎます。その段階でP&Rをやっても、あとでひっくり返るので、無駄になりそうです」→ 板の P&R はもう流さない。MIG・ip.tcl・XDC は commit 済み、gclk は parameter のまま。
  - 申し送り: 1 SU の KC705 の形で LUT が 105 %（combined）: 面積の削減（p036・p037 の続き、SU・TU の規模）か NSU・機能の選択が要る。生成物は amaterrace の worktree p2-p024 の build/vivado/（ip-all/ip が core、atg_kc705_raster/vivado.log）。

