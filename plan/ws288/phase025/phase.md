<!-- awesome-plan project=zedbsd record=ws288-p025 -->

# ws288-p025 timing の調整（KC705 で 200 MHz）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §8 の p025 の行、R3。
- 状態: planned（2026-10-24 Q1。新設: ユーザー「タイミングの調整と KC705 での確認は別なフェーズに分けた方がいいと思います。」）
- 依存: RTL が一通り揃った後（p009 以降。ユーザー「全体のRTLをまずは一通り揃えることを優先したいです。」）。

## 範囲

- KC705（xc7k325t）で GPU の全 pipeline を 200 MHz に。out-of-context の Vivado で WNS を測る。
- 分かっている所: ROP の blend（2026-10-24 の測定で WNS −4.207 ns、約 108 MHz。stage 3 を 2 段に分ける差分 [blend-stage-split.patch](../blend-stage-split.patch) が未適用）、ROP の wb（11k LUT）、RX・SU・TU の長い組み合わせの経路。
- p007・p009 の記録の「速さ」の残件（asm の warp の重なり、rx_frag の quad ごと約 200 cycle、TU の addr・cache・filter）は throughput の話で、p015 と分けて扱う。

## 受け入れ

KC705 の全 pipeline の構成が 200 MHz で WNS ≥ 0（out-of-context の Vivado）。sim の TB は全部 PASS のまま。

## 記録

- 2026-10-24 Q1: 新設。p007 の記録の「200 MHz の作業は p015（R3）でまとめて」をここに移した。
