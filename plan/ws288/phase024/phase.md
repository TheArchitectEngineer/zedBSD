<!-- awesome-plan project=zedbsd record=ws288-p024 -->

# ws288-p024 KC705 の板での確認

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p024 の行。
- 状態: planned（2026-10-24 Q1。新設: ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」、その前に「タイミングの調整はまだ行わないので、KC705 での動作確認は後にします。実装を優先し、次のフェーズへ進んでも OK です。」）
- 依存: [p012](../phase012/phase.md)（KC705 の RTL）。timing（[p025](../phase025/phase.md)）は待たない: 低い clock で確かめる。

## 範囲

- MIG の IP の生成（`vivado-build-ip.tcl`）、KC705 の bitstream（Vivado、必要なら openXC7）。
- JTAG の bridge（`am_jtag_regs`）で register を書いて BLT の fill → `DBG` で読み戻す（`plan/tools/kc705/`）。
- HDMI（ADV7511 を I2C で設定）で DC の color bar（ユーザーの目視）。
- p007（raster）・p009（shade・tex）の scene を板で流し、sim の結果と一致。

## 受け入れ

実機で fill → `DBG` の一致、color bar の目視、scene が sim と一致。QEMU の証拠は無い（実機だけ）。

## 記録

- 2026-10-24 Q1: 新設。p012 の part B（板）と、p007・p008・p009 で後回しにした KC705 の確認をここに集める。
