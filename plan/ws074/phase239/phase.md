<!-- awesome-plan project=zedbsd record=ws074-p239 -->

# ws074-p239: `clip: rect()`、abspos で伸びた button の中身の縦中央

B1 (2026-10-15)。Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p236〜p238 の続き。

## 変更

- `clip`（`auto`・`rect(top,right,bottom,left)`、comma も空白も、各辺は長さか `auto`）を CSS の property として追加（`css/values.c`・`css/cascade.c`・`css/css.h`・`css/internal.h`）。position が absolute・fixed の箱で、border box の左上からの矩形に描画を clip（`paint/list.c` の `list_clip_path`、auto の右・下は箱の端）。yahoo.co.jp・google.com などの visually-hidden の定型（`position:absolute; clip:rect(1px,1px,1px,1px)`）が効く。試験: [pages/clip-rect.html](../tests/pages/clip-rect.html)。
- p236 の回帰の修正: abspos の `top`・`bottom` 両方指定で伸びた `<button>` は、`position.c` が layout の後に高さを決めるので中身が中央に来なかった。`layout_center_button`（block.c、layout.h に宣言）を `position.c` からも呼ぶ。固定 WPT `css/css-position/position-absolute-semi-replaced-stretch-button.html` が p236 で PASS→FAIL になっていたのを PASS に戻した。（p236 では css-position を測っていなかった。）

## 確かめ（host）

- 固定 WPT reftest の前後（前は p236 直前の binary）: css-masking 45→58/280（`clip/clip-*` 13 件 FAIL→PASS、逆 0）、css-position 54/132 同数（p236 の後退を戻した、差 0）、CSS2/positioning 398/513 同数。build warning 0。Acid3 score 100・差 0、Acid2 差 873（不変）。
- yahoo.co.jp live: 広告の「この広告についての詳細」の重なりは残った（この要素の CSS を特定できていない。clip ではない別の隠し方）。google.com・yahoo の ink 一致は前後でほぼ同じ（yahoo 25.25%→25.75%、広告で動く）。
- 未実施: QEMU・実機。

## 残り

- yahoo の広告の「この広告についての詳細」が Chrome は見えない（ours は広告の上に重なる）。原因の CSS の特定。
- flex・grid の stretch で高さが後から決まる `<button>` の中身の中央（今は abspos だけ）。
