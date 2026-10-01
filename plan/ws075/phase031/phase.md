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

## 完了の条件（2026-10-01 追記、目標から）

- 画素: apps8.sh の各 app の撮影（`shots/a-*-live.png`）が base と batch で、時刻の文字と Gears の animation の外は同じ。
- C6: batch の image の measure-apps 5 run の c6.py の全体の中央値が base（p029 の 67.3 ms）より小さく、L3 の 50 ms 以内なら L3 を満たす
  （届かなくても画素が同じで縮めば cleared、L3 は未達と記録）。
- 止まらないこと: stress-117 の 100 回で `stress-117: no stop`。
- 回帰: QEMU の Venus で C7・C9（commands.md §5 の形で C7 C9）、WS079-p010、boot test PASS。

## 手順（2026-10-01 追記）

**着手の条件: ユーザーが描画の高速化の再開を言うこと**（2026-09-30 午後の指示）。compositor の変更なので main の許可も要る。
上の「再開の手順」の `build/ws075-p031/hw.sh` と `pixdiff2.py` は P1 の worktree にあり、今の repo に無い（2026-10-01 確認）。代わりに下の command を使う。
手引きは [guide.md](../guide.md) §5・§6。

1. worktree で patch を当てる。2026-10-01 の main では `git apply` は compose.c・glass.c で失敗し、`git apply --3way` は衝突なく当たる（`--check` で確認）:
   ```
   git apply --3way plan/ws075/phase031/exp/text-batch.patch
   git diff --stat
   ```
2. host で shader を確かめる（text.vert・text.frag が i915 の compiler で accepted、scoreboard sound）。`plan/ws075/tests/guard/run.sh` は panel・quad・
   browser の shader だけを見るので、text の 2 つは shaders.h から SPIR-V を出して `shader-dump` に掛ける（guard/run.sh の python の部分の名前を
   `zwl_text_vert`・`zwl_text_frag` に替えた形。名前は patch の shaders.h で確かめる）。既存の guard は `sh plan/ws075/tests/guard/run.sh` で PASS のまま。
3. image を 2 つ（base = patch の前の main、batch = patch の後）。順に、同時に build しない:
   ```
   plan/ws075/demo/build-demo-image.sh build/ws075-p031/base-pt passthrough ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"
   plan/ws075/demo/build-demo-image.sh build/ws075-p031/batch-pt passthrough ZEDBSD_GRAPHICAL_BOOT=n "ZEDBSD_BOOT_EXTRA_LINES=display=edp login=graphical"
   ```
   （base は patch を当てる前の tree で作る。worktree を 2 つ使うか、base を先に作ってから patch を当てる。）
4. 実機の passthrough（lock は script が取る。1 つずつ）:
   ```
   plan/ws075/tests/hdmi/measure-apps.sh build/ws075-p031/base-pt/hdd-image.img build/ws075-p031/base-pt/vmunix build/ws075-p031/base
   for n in 1 2 3 4 5; do plan/ws075/tests/hdmi/measure-apps.sh build/ws075-p031/batch-pt/hdd-image.img build/ws075-p031/batch-pt/vmunix build/ws075-p031/m$n; done
   python3 plan/ws075/tests/hdmi/c6.py build/ws075-p031/m1 build/ws075-p031/m2 build/ws075-p031/m3 build/ws075-p031/m4 build/ws075-p031/m5
   ```
5. 画素の比較: guide.md §6.6 の python を `build/ws075-p031/base/shots build/ws075-p031/m1/shots` で。
6. stress: guide.md §6.3 を batch の image で（OUTDIR `build/ws075-p031/s1`）。
7. QEMU の回帰: commands.md §5 の criteria の image を batch の tree で作り `criteria.sh ... C7 C9`、WS079-p010 の試験（`plan/ws079/phase010/phase.md` の command）、boot test（guide.md §5.4）。
8. 結果を「検証」に、passthrough と QEMU を分けて書く。素の 5330 は未実施と書く（ユーザーの実機の時だけ）。
