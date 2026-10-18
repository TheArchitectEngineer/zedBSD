<!-- awesome-plan project=zedbsd record=ws074-p234 -->

# ws074-p234: aspect-ratio と object-fit / object-position（WPT Interop 2023・2024 の layout）

Status: cleared（2026-10-18 Q1: Status の行が無かったので写した。元: cleared（host、2026-10-15 B1。aspect-ratio・object-fit・object-position、aspect-ratio の reftest 73→138。QEMU・実機は未実施））

B1 (2026-10-14/15)。ユーザーの指示「そのあとで、Interop 2024, 2023の、よく使われるレイアウトの部分を選んで改善しましょう。」Status: host の確認まで cleared 相当（QEMU・実機は未実施）。p233 の続き。

## 選んだ領域と理由

fixed WPT の reftest（`run-wpt-reftests.py --root css/css-sizing/aspect-ratio` ほか）を測ったところ、**`aspect-ratio` が engine に全く無く**（media query だけ）、`object-fit`・`object-position` も無かった。どちらも Interop 2021〜2023 の対象で、実 site のカード・thumbnail（`aspect-ratio:16/9`、`object-fit:cover`）で非常によく使われる。position:sticky・overflow の inner scroll は、engine の scroll がページ単位しか無く、静止画の見栄えにほぼ効かないので今回は見送り（sticky は reftest 20 件中 4 件 pass、`scrollTop` を前提）。

## 実装

- `css/values.c`・`css.h`・`internal.h`・`cascade.c`: `aspect-ratio`（`auto`・`<number> [/ <number>]`・両方）、`object-fit`（fill・contain・cover・none・scale-down）、`object-position`（1〜2 成分、keyword・長さ・百分率。transform-origin の解析器を共用）。
- `layout/block.c`: height が auto で ratio がある非 replaced の block は、幅から高さ（box-sizing の箱に対する比）。overflow が visible なら中身が超えうる下限、clip/scroll なら definite。width が auto で height が px なら、幅を ratio から（block_width）。
- `layout/position.c` `layout_shrink_to_fit`（float・abspos・inline-block・flex item）: auto 幅 + px の高さ + ratio で幅を高さから（visible の箱は min-content を下限）。
- `layout/flex.c`: row の flex item で flex-basis auto・幅 auto・px の高さ・ratio なら base を高さから。
- `layout/replaced.c`: `aspect-ratio` が画像の ratio を置き換える（`auto` は画像の ratio を優先、無ければ指定の ratio）。border-box では frame を含めた箱の比。
- `paint/list.c` `list_image`: object-fit・object-position に従って画像を置き、はみ出す分は clip する。
- 試験: [object-fit.html](../tests/object-fit.html)・[object-fit-2to1.png](../tests/object-fit-2to1.png)（fill・contain・cover・位置・none・scale-down・aspect-ratio）。結果 PNG: [b1-object-fit.png](../../../build/review/b1-object-fit.png)。

## 前後の数（同じ WPT revision `2d66b9b7…`、reftest、`--limit 0`、HEAD の binary と今回の binary）

| 範囲 | 前 pass | 後 pass | PASS→FAIL |
| --- | ---: | ---: | --- |
| css/css-sizing/aspect-ratio（257 件） | 73 | **138**（+65） | 0 |
| css/css-images（405 件） | 118 | 123（+9、−4） | 4（下記） |

- css-images の PASS→FAIL 4 件（`conic-gradient-line-height-relative-units-003/004`・`radial-gradient-…-003/004`）は、`aspect-ratio:1` を持つ箱が高さ 0 のまま赤い box が見えず偶然 pass していた物。ratio が効いて箱が出るようになり、`mask-image`（未実装）が効かず赤が見える。layout は正しくなった側の変化。
- 残る aspect-ratio の fail（119）: flex の item（`flex-basis:0` と中身の min-content、column）31、grid の item 21、replaced 20、block の細部 15、intrinsic-size 12、abspos の inset 指定 10 ほか。

## 確かめ（host）

- build warning 0。host JS 17/17、DOM 23/23、http 15/15、loader 11/11、fetch `--async` 38/38、CSS rule model 51/51、Grid 23/25（`abspos-static`・`auto-track-css` は p232 から既存）。Acid3 score 100・差 0、Acid2 差 873（不変）。固定 WPT 1,455 ID: 前 483 pass・callback 10,983、後 483 pass・callback 10,983、ID 単位の差 0。google.com live: ink 73.35%（p232 と同じ）、script error 1。
- yahoo.co.jp live（feed）: 記事の画像が Chromium と同じ位置・大きさで出る（[ours | Chromium](../../../build/review/b1-yahoo-p234.png)、広告は request ごとに違う）。
- 未実施: QEMU・実機。

## 残り

- flex・grid の item の aspect-ratio（上の内訳）、abspos の inset 指定の ratio、`min-width:auto` の content 下限との組み合わせ。
- `mask-image`、`position: sticky`（scroll 時の動き）、inner scroll container の `scrollTop`。
