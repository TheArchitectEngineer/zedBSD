<!-- awesome-plan project=zedbsd record=ws288-p041 -->

# ws288-p041 SU の clock の領域（CDC、sclk ↔ gclk）

- 親: [WS288](../ws.md)。設計: [design.md](../design.md) §19.4・§19.7〜§19.10 の T1。
- 状態: planned、disposition canceled（2026-10-26 22:53 Q1、WS288 の終了）。前の状態: planned（2026-10-26 02:10 Q1 新設）
- 由来: U109（2026-10-25 23:46 ユーザー「U100-U109ですが、推しで良さそうです。」）: CDC の実装は機能の正しさを変えるので p025 から別の Phase に分ける。
- 依存: p025 の T2（SU の pipeline）と並べられる（file の所有は Q1 が決める。`atg_top`・`atg_cp_ctl`・VF・RX は p025 の T3 と重なる）。

## 範囲

design §19.10 の T1 の行: `am_cdc_*`・`am_rst_cdc` と TB、`atg_su_cdc`、VF・RX の back-pressure、要求の entry と許可の event、frag の copy の SU の中への移し、clip の読みの streaming、command の FIFO と CP の credit・DRAW の前の待ち、status の word と idle の数え方、fault の latch、`atg_cp_ctl` の reset の握手、`atg_kc705_clk` の VCO 1200 MHz と出力、XDC（§19.4.4）、OOC の tcl の 2 つの clock、sim の乱数の同期の model。ASYNC の parameter で同じ clock の形にも戻せる（U103）。

## 受け入れ

§19.9 の CDC と scene の TB（word 一致、比の違う clock、乱数の同期の seed を複数）、`report_cdc` の Critical 0、2 つの clock の `atg_top` の OOC。sim の TB は全部 PASS のまま（T）。目標: gclk 100 MHz・sclk 150 MHz（2026-10-26 02:10 ユーザー）。

## 記録

- 2026-10-26 02:10 Q1: 新設（U109）。
- 2026-10-26 22:53 Q1: 取り下げ（canceled）。WS288 の終了（2026-10-26 ユーザー「amaterrace-gpuは他のエージェントにまかせることにしました。あなたにはzedBSD側に集中してもらいます。よって、ws288をクローズしてください。」）。amaterrace-gpu 側の作業は amaterrace-gpu の plan（am-ws001 など）へ、zedBSD の GHCI の driver は [WS290](../../ws290/ws.md) へ。この Phase の成果と記録はここと git の履歴に残す。
