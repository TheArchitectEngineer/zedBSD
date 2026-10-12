<!-- awesome-plan project=zedbsd record=ws211-p007 -->

# ws211-p007: 窓の dock・undock のアニメを、窓の絵の拡大縮小にし、終わってから client に描き直させる

Parent: [WS211](../ws.md)
Status: in-progress（設計と実装、2026-10-12 P3。Pi は place16 で確かめる）
Phase disposition: normal
Queue: P3（2026-10-12 Q1 の割り当て）

## 由来

- 2026-10-12 ユーザー（place15 の評価）:「ウィンドウのドック・アンドックのアニメ。2フレームくらいしか描かれない。ここは、ウィンドウの内容を維持したまま拡大縮小でアニメーションしてしまって、拡大縮小が終わってからゆっくり再描画を書けるのがいいと思うな。」
- Q1: アニメの間は client に configure の大きさの変更を送らず、既存の buffer の絵を GPU で拡大縮小して描く。終わった時に configure を送り、client が新しい大きさで描く。p002 の layer の絵の仕組みを使う。

## 今の経路と実測（source と place15 の log、scratchpad/p3g3/b15）

- `window_dock`・`window_undock`（`shell.c`）は、アニメ（`DOCK_MS` 120ms、`server->anim`）を始めると同時に `window_configure` で新しい大きさを送る。client（例 Monitor、1904×1020）はアニメの間に新しい大きさで描き、import する（log では 7.7MB の import が 3 回）。Pi は CPU が 1 つ（SMP は WS208）なので、client の描画と compositor のコマが CPU と GPU を取り合う。
- アニメの 1 コマは全画面（`server->dirty`）。log の dock では、コマ 185（t=0.35）と 186（t=0.99）の 2 コマだけ。どちらも記録 10〜13ms、submit から表示まで 56〜64ms。88〜95 draw（layer 40、bar 48〜55）。同じくらいの全画面のコマは、client が描かない時は render 28ms（v3d perf）。
- アニメの窓の body は `draw_body` が毎コマ描く: 影、glass（窓が透ける時）、client が宣言した glass の panel（Monitor は 12 枚、各 panel に影と glass）、sub-surface、client の絵（今の buffer を body の矩形へ伸ばす）。
- アニメが終わると、docked の窓は client が新しい大きさを描くまで、古い絵を拡大せずに左上から切って描く（BUG-243 の規則、`draw_body` の `unscaled`）。undock は送った大きさに伸ばして描く（BUG-180、`body_rect` の `resized_ms`、最大 `RESIZED_HOLD_MS` 1 秒）。

## 設計

### 1. configure をアニメの終わりに送る

- `window_dock`・`window_undock` は configure を送らず、`server->anim_configure = 1` を付けてアニメを始める（新しい `anim_begin`）。アニメの間、client は古い大きさのまま。commit すればその絵が伸ばされて描かれる（今と同じ）。
- アニメの終わり（`kwl_glass_tick` で進みが 1 になった時の `anim_finish`）に `window_configure` と `window_resized` を呼ぶ。送るのはその時の状態（maximized・大きさ）。
- 別の窓のアニメが始まる時（launch など）は、前の窓の保留の configure を先に送る（`anim_begin` が `anim_finish` を呼ぶ）。同じ窓のアニメが重なる時（dock の途中で undock、pull back）は保留を引き継ぐ（OR）。最後の状態を 1 回送れば足りる。
- 窓が壊れた時（`objects.c`・`protocol.c` が `server->anim = NULL`）は送らない。`window_float_quiet` は自分で configure を送る（今と同じ）。
- pull back（`pull_back`）は configure を送らない（大きさは docked のまま）。launch も送らない（今と同じ）。

### 2. 終わってから client が描くまで、古い絵を伸ばす

- dock のアニメの後、client が docked の大きさを描くまでの間（`resized_ms` の待ち、最大 1 秒）は、`unscaled` にせず docked の場所に伸ばして描く。アニメの最後のコマと同じ絵が続き、client の新しい絵に替わる。BUG-243（キーボードの panel で docked の場所が変わった時）は `resized_ms` を立てないので、今どおり切って描く。
- この間、入力の座標は今どおり（docked の窓は拡大なしの対応、BUG-243 の注記）。伸ばした絵と入力が合わない時間は client が描くまで（Pi で 200〜300ms 程度）。undock の BUG-180 と同じ扱い。

### 3. アニメのコマを 2 枚の絵で描く（p002 の layer の絵を使う）

- layer-cache に 3 枚目の絵 `KWL_LAYER_WINDOW`（出力の大きさ、8MB）を足す。
- アニメの最初のコマで、そのコマの pass の前（in-band、p002 と同じ）に 2 枚を描く。
  - `KWL_LAYER_DESKTOP`: 壁紙・デスクトップの icon・アニメの窓を除いた窓（`kwl_glass_draw_desktop_picture`、`layer_skip`、p002 のまま）。
  - `KWL_LAYER_WINDOW`: アニメの窓の body の影を除いた全部（glass、panel、sub-surface、client の絵）を、アニメの始めの矩形（`anim_from`）に、画面と同じ座標で描く（glass が壁紙の blur を正しい場所から取るため）。
- 以後のコマ（最初のコマも）は、壁紙の代わりに desktop の絵を 1 quad、絵に無い窓（後から map した窓）を今どおり、アニメの窓は影（今どおり、今の矩形）＋窓の絵の始めの矩形の部分を今の矩形へ拡大縮小した 1 quad（角の丸め付き）＋title bar（今どおり）。bar は今どおり。
- 絵を使う条件（最初のコマで決め、毎コマ確かめ、外れたらそのアニメの残りは今どおり全部を描く）:
  - dock・undock・pull back のアニメ（launch は除く）。
  - App Home が閉じて止まっている、デスクトップの slide・Wiseview・drag・pull・dnd が無い、全画面の窓だけのコマでない。
  - アニメの窓は server の装飾の窓（CSD でない）、sheet でない、anchor の出力の窓で、始めの矩形が出力の中に全部入る。
  - 見えている窓に、docked の中央の窓（`window_centred_over`）、`set_blur` の窓、並べの glide 中の窓が無い（それらは backdrop を毎コマ描くので絵にできない）。
  - head の pass（`kwl_glass_draw_head`）は今どおり（絵は anchor の pass だけ）。
- 見た目の違い（受け入れる物、ユーザーの p002 の決定と同じ種類）: アニメの間、他の窓の中身とアニメの窓の中身は始めの時の絵のまま（120ms）。アニメの窓の glass は始めの場所の壁紙の blur のまま。拡大の時、角の丸めは絵の角を伸ばした分だけ大きく見え得る。
- 費用: 最初のコマは 2 枚を描く分だけ重い（desktop の 1 回分と窓の body 1 回分）。以後は、他の窓と panel の draw（Monitor では約 30 の glass・影）が 2 quad になる。

## 試験

- host: rpi4 の wayland の build で warning 0。amd64 の build。
- Pi（place16）: 窓（Monitor・Settings・Files）を double click で dock・undock、最大化ボタン、title を bar へ drag、bar から引き下ろす。見ること:
  - アニメのコマ数（`KWL GLASS anim` の行の数、t の進み）。5 コマ以上を目安。
  - `KWL LAYER baked what=anim` と `KWL LAYER use pictures=N drawn=M`。
  - `KWL CONFIGURE` がアニメの終わり（`KWL GLASS anim ... t=1.00` の後）に出ること、`KWL GLASS resized` の after_ms。
  - アニメの後、client が描くまで窓が伸びた絵のままで、左上に小さく切れて出ないこと。
  - v3d perf の render の時間。
- T1（i915・Venus、WS の最後にまとめて）: dock・undock の見た目、`KWL VULKAN_ERROR` が出ないこと。

## 未決

- `DOCK_MS`（120ms、BUG-179 のユーザーの「0.1 秒、長くても 0.2 秒」）は変えない。コマが増えても足りなければ、ユーザーに長さを相談する。
