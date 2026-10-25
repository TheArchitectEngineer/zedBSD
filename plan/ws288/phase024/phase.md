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
