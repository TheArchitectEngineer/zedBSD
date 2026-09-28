<!-- awesome-plan project=zedbsd record=ws081-p010 -->

# ws081-p010: Files への適用（tap・長押し・scroll と慣性）

<!-- awesome-plan-current:start -->
Status: planned（2026-09-29、実装に入る前に計画に無い依存を見つけたので止めた。main の判断待ち）
Disposition: normal
Parent: [WS081](../ws.md)
Queue: main の指示（2026-09-29「p010（Files）→ p011（Terminal）の慣性の scroll」）。Awesome Plan の Queue の item ではない
Resume point: 下の「要る判断」の答えの後
<!-- awesome-plan-current:end -->

## 見つけた依存（実装の前）

design §4.1 の (a) では、wl_touch を bind した client は、指を pointer の代わりとして受けなくなります（compositor の `touch.c`）。Files で慣性の scroll を作るには
wl_touch が要り、tap（click）・長押し（context menu）・drag（scroll か pointer の drag）を Files の側で作り直すことになります。そのうち次のことを確かめました。

- **context menu**: compositor（`menu.c`）は最新の press の serial で開きます。wl_touch の down も `press_serial` を更新するので（`touch.c`）、指の down の serial で開けます。
- **drag and drop**: compositor の `start_drag`（`data.c`）は、pointer の button が押されている（`server->buttons_down`）時だけ drag を受けます。drag はその後
  pointer に付いて動きます。wl_touch の client の指は pointer の button を押さないので、Files が指で drag and drop（file を folder・sidebar・他の窓へ運ぶ）を
  始めると、compositor が拒みます（`ZWL DATA drag refused`）。今は Files が wl_touch を持たず、指は compositor の pointer の代わりとして button を押すので、
  指の drag and drop ができています。Files に wl_touch を入れると、これが後退します。
- 直すには compositor の `data.c` で、wl_touch の client の指の暗黙の grab（down の serial）から drag を始め、drag をその指に付けて動かし、指の up で drop する
  必要があります。この subagent に許された compositor の範囲（`touch.c` と入力の処理）の外です。

## 要る判断（main へ）

1. compositor の `data.c` に touch の drag and drop を入れる（Phase を足す）。Files の p010 はその後。
2. Files の指の drag and drop を諦めて p010 を進める（長押しの後の drag は範囲選択だけ、など）。
3. Files は今の pointer の代わりのまま（慣性なし）にし、p010 を取りやめる。

既定の案（意見）: 1。file manager の指の drag and drop は中心の操作で、Terminal・Notes 等の他の app の指の drag（選択の文字の drag）にも同じ仕組みが効きます。

## 確認

実装に入っていないので、試験はありません。調べたのは compositor の `data.c`（`start_drag`）、`menu.c`（context menu の serial）、`touch.c`（`press_serial`）と、
Files の scroll の持ち方（`tab->scroll`・`sidebar_scroll` の整数の px。範囲の外の値も描画の計算は壊さない）です。
