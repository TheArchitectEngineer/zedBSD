<!-- awesome-plan project=zedbsd record=ws074p062 -->

# ws074-p062: 描画（border-radius・box-shadow・opacity・outline）

Phase ID: `ws074-p062`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p014

## 範囲（ws.md の表: p038 から。amazon-goal.md §4 の 7）

`border-radius`（CPU と GPU）、`opacity`、`box-shadow`、`outline`。amazon-goal.md の 7 に並べた `linear-gradient`・`object-fit` は
この Phase で行っていない（下の「残り」）。`box-shadow` を正しく描くのに要った `clip-path: inset()` を足した（下）。

## 設計

- **display list だけで表す**: 角丸・影・outline・opacity を既存の item（矩形・文字）で組み立て、CPU と GPU の renderer・shader は
  変えない。両方の renderer が同じ item を描くので一致し、実機の GPU（kernel の SPIR-V の compiler）に新しい shader を通す危険が無い。
  - 角丸: 角の画素の行ごとに、行の中央の高さでの曲線（楕円）の間の矩形（横は面積の被覆で AA）。角の間の直線の部分は 1 つの矩形。
  - 角丸の枠: 外の形（border box）と内の形（padding box、半径は外から枠の幅を引く）の間を行ごとに、上下の行はその辺の色、
    padding box の横の行は左右の辺の色。
  - 影: border box の形を offset で動かし spread で広げ、blur は 10 枚の半透明の複製を正規分布の分位点（σ = blur/2）の分だけ
    広げ・縮めて重ねる（重なった数の割合がその点の Gaussian の値になる。各複製の alpha は全部で元の alpha になるよう）。
    内側の影（inset）は描かない。影は box の背景の下に描く（透明な背景の box では box の中にも見える: 範囲の外）。
  - opacity: box とその中身に描いた矩形・文字の alpha に掛ける（group の合成ではなく item ごと。重なる子の色は Chromium と少し違う）。
    0 の box は中身ごと描かない。inline の box の opacity はその中の文字に掛ける。画像は掛けない。
  - outline: border box を outline-offset だけ広げた周りの 4 つの矩形（角丸には沿わない）。box と中身の後に描く。
  - clip-path: inset(): box の描画（影を含む）を border box から inset だけ動かした矩形で clip（`PAINT_CLIP`）。Amazon のトップの
    `#gwm-Deck { box-shadow: 0 0 0 100vw #F0F2F2; clip-path: inset(0 -100vw) }`（左右だけに伸びる影）が無いと header を覆った。

## 実装

- CSS（`css/css.h`・`internal.h`・`values.c`・`cascade.c`）: longhand `radius`（角 4 × 横・縦、px・%・calc）、`opacity`（数・%）、
  `box-shadow`（comma の列、4 つまで、inset・長さ 2〜4・色、`CSS_VALUE_SHADOWS`）、`outline-width`・`-style`（auto は solid）・
  `-color`（invert は currentcolor）・`-offset`、`clip-path`（inset() だけ、他の形は clip しない、`CSS_VALUE_INSET`）。shorthand
  `border-radius`（`/` の縦の列）・`border-*-*-radius`（1〜2 値）・論理の `border-start-start-radius` 等・`-webkit-`・`-moz-` の別名、
  `outline`、`-webkit-box-shadow`・`-moz-box-shadow`。inherit・initial の展開、currentcolor の解決（outline・影）。
- 描画（`paint/list.c`）: `list_box` を opacity・clip-path・outline の包みにし、元の本体を `list_box_own` に。`list_border_shape`
  （% の解決と重なる半径の縮小）、`list_round_fill`・`list_round_row`・`list_round_span`、`list_round_borders`、`list_shadows`・
  `list_shadow`・`list_grow_shape`、`list_outline`、`list_fade`、`list_inline_opacity`、`list_clip_path`。
- 試験: `tests/pages/decoration.html`（新、golden 4 つ）: 半径 4 種・pill・円・角ごと・楕円・`/`・角の longhand と `-webkit-`、角丸の枠
  （細い・太い・辺ごとの色）、影（Amazon の card の影・硬い影・blur・spread・2 つ）、opacity（0.5・30%・0・入れ子）、outline と offset、
  inline の opacity、Amazon の button 風。`flex.html`・`inline-block.html` の paint の golden を更新（角丸を使う部品）。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check（変更した source）: 新しい指摘 0（`cascade.c` の 1 件は既存）。
- 回帰（plain と ASan）: golden 56/56（decoration の 4 つを追加、flex・inline-block の paint を更新）、host-view 59/59、host-form 28/28、
  host-link 22/22、host-position 19/19、host-text 20/20、host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、
  run-dom-tests 6/6、run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17。ASan の `--render` で Amazon の 3 つの capture に報告なし。
- `render-compare.py decoration.html`（800x520）: 98.33%（直線の blur では 97.90%、分位点の blur で改善）。`chrome-boxes.py`: 33/33。
  違い: 角の縁の AA、opacity の group の合成（重なる子）、blur の裾。
- `gpu-compare.py decoration.html`（host の lavapipe）: 最大の channel の差 3、2 を超える画素 0.0092%（一致）。
- Amazon（2026-09-28 23:52 の capture の `*-local-noscript.html`、1280x900）:

  | page | p060 の後 | p062 の後 |
  | --- | --- | --- |
  | トップ | 画素 34.15%、ink 23.65% | 画素 34.16%、ink 24.28% |
  | 検索 | 画素 70.48%、ink 22.80% | 画素 68.83%、ink 20.06% |

  検索の欄・card・button に角丸と影が付いた。検索の数が下がったのは、左右が逆の列（`direction: rtl`、p060 の phase.md）の中で
  新しく描いた影と角丸がずれた位置にあるため。clip-path を入れる前はトップが 27.60% に下がった（上の `#gwm-Deck`）。
  `--render` は host で検索 1.52 s、トップ 0.59 s。
- guest（QEMU の Venus、zdesktop 1280x800）: `browser-page.sh decoration.html` status 0（host と同じ描画、GPU の path）、
  `browser-p060.sh` status 0、`browser-p032.sh` status 0。live の Amazon はこの Phase では取得していない。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p062-20260929-decoration.png`（私たち | Chromium | 違い）、`p062-20260929-amazon-top-local-noscript.png`・`…-search-local-noscript.png`。
- guest（QEMU）: `p062-20260929-guest-decoration.png`。

## 未実施・残り

- 実機は未実施。
- `linear-gradient`（Amazon に 103）・`object-fit`（72）: amazon-goal.md の 7 の残り。次の描画の Phase（p038 の一部）へ。
- 内側の影、背景画像の角丸の clip、`overflow: hidden` の角丸の clip、outline の角丸、opacity の group の合成（offscreen）、画像の opacity、
  box の中に見える影（透明な背景）、`clip-path` の inset 以外の形と `round`。
