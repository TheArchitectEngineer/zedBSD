<!-- awesome-plan project=zedbsd record=ws075p031 -->

# ws075-p031: L3: 文字の draw をまとめる（compositor）

Phase ID: `ws075-p031`
Parent: [WS075](../ws.md)
Status: uncleared（2026-09-30、P1。優先の低下で中断: ユーザーの指示「いったん描画の高速化はラップアップして、優先度が高いWSやPhaseの中では優先順位を下げ、機能性の実装にフォーカスしましょう。」（Q1 経由）。実装は build まで済んだが、画素の比較と C6 を測る前に止めたので source は戻し、差分は `exp/text-batch.patch` に置いた）
Phase disposition: normal
承認: 2026-09-30 Q1 の指示（p030 の手の 1 番目）。
再開の条件: ユーザーが描画の高速化の再開を言うとき。

## 目標（p030 から）

1 つの文字列の glyph を 1 回の draw にまとめ、frame で約 8〜10 ms 減らす（p030: 小さな draw 1 つ 約 25 µs、文字 約 400 draw/frame）。見た目は変えない
（画素の比較）。判定は C6 の中央値（5 run、Settings を含む 10 app）、回帰は stress-117 の 100 回・C7・C9・WS079-p010・boot test。

## 実装（`exp/text-batch.patch`、main には入れていない）

compositor の文字の描画の経路だけ（glass.c・compose.c・compose.h・backdrop.c の flush の 2 行・shaders）。keyboard.c は変えない（keyboard の
panel の文字も glass の shape を通るので同じく batch される）。

- shader: `shaders/text.vert`・`text.frag`（panel.frag の text mode（5）と同じ式: `colour = vec4(tint.rgb, 1.0) * tint.a * cover; result = colour * fade;`、
  色と opacity は flat）。`regenerate.py` に足して shaders.h を作り直した（既存の 4 つの shader の bytes は変わらないことを確かめた）。i915 の
  compiler（host の shader-dump）で両方 accepted、scoreboard sound。
- compose.c: text の pipeline（panel と同じ blend と layout、vertex は place・atlas の uv・色・opacity の 9 float）と、frame の glyph を書く
  vertex buffer（8192 glyph 分、host-visible で map したまま）。frame の開始で空に。
- glass.c: `glass_shape_draw` で、atlas を読む text mode の shape は描かずに vertex buffer へ 6 頂点（panel.vert と同じ NDC の角と uv の端）を
  書いて待たせる。それ以外の shape を描く前、窓の image（compose_quad_part）の前、render pass を終える前（backdrop の 2 か所・frame の最後）に、
  待っている glyph を 1 回の vkCmdDraw で描き（`zwl_text_flush`）、quad の corners の vertex buffer を bind し直す。描く順は変わらない。
- build: compositor・demo の image（warning 0）、style-check の新しい指摘 0。

## 検証

実機（5330 の passthrough）の base 1 run の途中でラップアップの指示が来て止めた（機械は返した）。画素の比較・C6・stress・C7・C9・WS079-p010・
boot test はどれも未実施。

## 再開の手順

1. `git apply plan/ws075/phase031/exp/text-batch.patch`（main がこの辺りを変えていれば直す）。
2. `build/ws075-p031/hw.sh` の形（base と batch の image、base 1 run の撮影、batch の stress 100 回と measure-apps 5 run）で測り、各 app の撮影を
   `pixdiff2.py` で base と比べる（上の bar の下で画素まで同じ）。
3. QEMU の Venus で C7・C9・WS079-p010、boot test。
