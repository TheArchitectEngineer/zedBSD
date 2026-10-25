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
