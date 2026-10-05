<!-- awesome-plan project=zedbsd record=ws090-p017 -->
# ws090-p017: 設計 — libkeiland の慣性 scroll（全ての窓）と開始の遅れ

Parent: [WS090](../ws.md)
Status: planned（2026-10-06 Q1 の設計の第 1 版。実装に取りかかれる）
Disposition: normal
Related: [BUG-211](../../bugs/BUG-211.md)・[BUG-218](../../bugs/BUG-218.md)

## 由来（ユーザー、2026-10-06 UAT）

BUG-211「慣性スクロールが実装されていないようです…この機能はlibkeilandのUI機能に入れて、どのウィンドウでもスクロールバーを使うときに有効にすれば利用できるようにしたいです。」BUG-218「Phoneアプリで慣性スクロールが効きました！でも、スワイプ動作から300msくらい遅れてスクロールが開始します。これは可能な限り小さく、50ms以内にしたいです。どんなに遅くても80msです。」

## 設計

- 場所: `userland/desktop/libkeiland/ui/scroll.c`（scroll の状態）に慣性を入れ、`list.c`・`scroll-bar.c` を使う全ての widget と、自前の scroll を持つ app（Phone・Files・Settings・Mail・Calendar）が同じ口を使う。Phone の独自の慣性は外して共通に寄せる。
- 速度の推定: 2 本指の scroll の event（wl_pointer の axis と axis_source=finger、axis_stop）の直近 100 ms の移動と時刻の最小二乗。指が離れた（axis_stop）時の速度で慣性を始める。
- 減衰: 指数（時定数 325 ms、iOS に近い）、速度が 20 px/s 未満で止める。端で止め、軽い弾み（overshoot 24 px まで、200 ms で戻る）は option（既定 on）。
- 新しい指の接触（axis の event）で慣性をすぐ止める。
- **開始の遅れ（BUG-218）**: 指の動きの最初の axis の event を受けた frame で scroll を動かす（gesture の判定や速度の窓を待たない）。今の 300 ms の内訳（compositor の gesture の判定の待ち、app の frame の待ち）を測り、compositor の 2 本指の scroll は「端の gesture でない」と決まるまで client に送らない今の待ちがあれば、端の band の外で始まった接触は即座に送る。目標: 指の動きから最初の再描画まで 50 ms 以内、最大 80 ms。
- 描画: 慣性の間は frame の callback ごとに 1 回だけ動かす（BUG-221 の frame rate の丸めと共通、[ws090-p018](../phase018/phase.md)）。
- API: `kl_scroll_set_kinetic(scroll, bool)`（既定 on）、`kl_scroll_tick(scroll, now)`（frame ごと）。KL_VERSION を上げる。

## 試験と Phase

- host: 速度の推定・減衰・端の止まり・新しい接触での停止。
- AAT: `apps.settings.kinetic-scroll`（wifi の一覧）、`apps.phone.scroll-latency`（注入の時刻から再描画の log まで）。実機はユーザー。
- 実装: p017a（libkeiland の慣性と API、host 試験、0.7 LW）、p017b（各 app への適用と Phone の独自の慣性の置き換え、開始の遅れの測定と短縮、0.7 LW）。
