<!-- awesome-plan project=zedbsd record=ws081-p006 -->

# ws081-p006: ブラウザの慣性の scroll と touch の入力（browser の shell）

<!-- awesome-plan-current:start -->
Status: uncleared（2026-09-29、WS081 の作業用サブエージェント。実装に入る前に止めた: 依存する WS074 の `browser.h` の API が main に無い）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29 夕「p010 → p015 → p006」、同日「要る WS074 の出力が main に無ければ uncleared で終え、要る API を報告」）。Awesome Plan の Queue の item ではない
Resume point: WS074 が下の「要る API」を `include/libc/browser.h` に入れて main に merge された後。shell（`userland/desktop/browser/shell/`）に wl_touch と libkeiland の scroller・gesture を入れる
<!-- awesome-plan-current:end -->

## 目的

Files・Terminal・PDF Viewer・Notes と同じ手触りで、ブラウザの頁を指で scroll する（慣性、端の rubber band、catch）。tap は click、長押しは右の button。
touch の処理は browser の shell（`/bin/browser` の窓の側）に置き、libbrowser・js・layout の内部は変えない（main の指示、2026-09-29）。

## 確かめたこと（main ff6e92ee の後の 36509f70、2026-09-29）

`include/libc/browser.h`（`BROWSER_API_VERSION` 1）と `userland/desktop/browser/view/view.c` を読んだ。

- scroll に関する公開の API は `browser_view_scroll_y`（読むだけ）、`browser_view_document_height`、`browser_view_wheel`（delta の px）だけ。
- engine の scroll は頁の根の縦だけ（`view->scroll_y`、`view_scroll` が `[0, 文書の高さ − view の高さ]` に抑える）。
  - 位置を設定する公開の API は無い。
  - 端を越えた分（overscroll）を描く手段は無い。
  - 入れ子の scroller（`overflow: auto` の要素）も無い。
- `browser_view_wheel` は頁の script に WheelEvent を先に渡し、cancel されなければ scroll する。
  - shell が慣性を毎 frame の wheel の delta として送ると、glide の間ずっと script に偽の WheelEvent が届き、script が止めることもできる。
  - 端での rubber band も、delta を使い切ったかの返りも無いので作れない。
  - design §6・§8 の懸念のとおり。

よって design §8 の依存（p006 は WS074 の `browser.h` の変更に依存）が満たされていない。

## 要る API（WS074 への依頼の案。名前は WS074 が決めてよい）

最小（これで p006 を始められる）:

1. **scroll の位置の設定**: `int browser_view_scroll_to(struct browser_view *view, double x, double y);`
   - 頁の根の scroll を px で置く。WheelEvent は出さず、頁には `scroll` event だけを出す。
   - 範囲の外は範囲に抑える（下の 3 の overscroll は別）。redraw の callback を呼ぶ。
2. **scroll の範囲**: `int browser_view_scroll_range(struct browser_view *view, double *largest_x, double *largest_y);`
   - 今の layout での最大の scroll（文書の大きさ − view の大きさ、0 以上）。layout が要れば行う。
   - `browser_view_document_height` と view の高さから shell でも計算できるので、無くてもよい。ただし横の scroll と、layout の遅延（画像の到着で高さが変わる）を engine が一か所で扱える。
3. **overscroll の描画**: `int browser_view_set_overscroll(struct browser_view *view, double dx, double dy);`
   - scroll の位置を変えずに、内容を dx・dy px ずらして描き、ずらした隙間は頁の背景色で埋める。
   - rubber band の見た目（端を越えて引く・ばねで戻る）は shell の scroller が計算し、毎 frame これで渡す。

あるとよい（後の Phase でもよい）:

4. **点の下の scroller と、使い切ったかの返り**: `int browser_view_scroll_by(struct browser_view *view, float x, float y, double dx, double dy, double *used_x, double *used_y);`
   - 指の下の最も内側の scroll できる要素から親へ順に scroll する（scroll chaining）。
   - 使った量を返し、余りを shell が overscroll にする。`overflow: auto` の入れ子の scroller が engine に入った時に要る。
5. **頁の script への touch**: `browser_view_touch(view, id, kind, x, y, modifiers)` で TouchEvent・PointerEvent（`pointerType: "touch"`）を渡す。
   - `preventDefault`・`touch-action` の結果を返す（cancel された touch は shell が scroll しない）。
   - これが無い間、shell は tap を `browser_view_pointer_button` の click として渡す（p010〜p012 と同じ）。

shell の側（WS081 p006 で行うこと）: wl_touch の bind、libkeiland の scroller・gesture。
- 一本指（二本指も重心で）の drag で `browser_view_scroll_to`、端の先は `browser_view_set_overscroll`。
- tap は primary の click、長押しは secondary の button。
- key・wheel・link の移動などで scroll が外から変わったら、指はそれを引き取って glide を止める（`browser_view_scroll_y` の変化で知る）。

## 確認

実装に入っていないので、試験はありません。読んだ file: `include/libc/browser.h`、`userland/desktop/browser/view/view.c`（`view_scroll`・`view_clamp_scroll`・`browser_view_wheel`）、
`userland/desktop/browser/shell/`（file の一覧）、`plan/ws081/design.md` §6・§8、`plan/ws074/ws.md`。

## 要る判断（main へ）

- 上の 1〜3 を WS074 に依頼する（main が WS074 に依頼する、2026-09-29 の指示）。4・5 は WS074 の計画（入れ子の scroller・TouchEvent）と合わせて決める。
- 代わりの案（意見としては取らない）: 今の `browser_view_wheel` だけで慣性を作る。rubber band は無く、glide の間に script へ偽の WheelEvent が出続ける。
