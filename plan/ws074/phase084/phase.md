<!-- awesome-plan project=zedbsd record=ws074p084 -->

# ws074-p084: Amazon の画素の差の原因の調べ（DOM・layout）

Phase ID: `ws074-p084`
Parent: [WS074](../ws.md)
Status: uncleared（2026-09-29、main の wrap up で停止。調べと Phase の案は済み、直した layout の確認の一部が残り）
Phase disposition: normal
Queue: なし（main の指示でサブエージェントが worktree `wt/ws074-dom` で実行、2026-09-29）
依存: p031・p080〜p083
並行: JS の側（p087: Symbol・iterator・for-of・Map・Set）が `js/`・`vm/` を変える。この Phase は `js/`・`vm/` を変えない。

## 目的と範囲

- top-local 64.47%・search-local 76.04%（`live-compare.py`、script 付き、1280x900）の差の主な原因を、面積の大きい順に特定する。
- 直すべき項目を Phase の案として並べる（効果の見込みと大きさ）。小さく確実に直せるものはこの Phase で直してよい（main、2026-09-29）。

## 調べ（途中、2026-09-29）

基準（main の p083 の後、host）: top-local 64.47%、search-local 76.04%、script を除いた capture（Chromium も script なし）で
top-local-noscript 79.80%、search-local-noscript 77.16%。worktree の `build/p084/live-*/`（ours・chromium・side の PNG）。

50 px の帯ごとの不一致（%、y の上端）:
- top-local: 0〜600 は 14〜28、650:47、700:99、750:98、800:99、850:26。
- top-local-noscript: 0〜600 は 12〜26、650:44、700:50、750:9、800:12、850:12。
- search-local と search-local-noscript はほぼ同じ形（0〜550 は 4〜39、550〜800 は 29〜47）。

### 1. 動的に挿入された script が走らない（最大、top の約 13 点）

- top の script 付きと noscript の差（700〜850 の帯）は、Chromium では footer の上に「最近閲覧した商品とおすすめ商品」（`#rhf`）の
  枠が出て footer が約 130 px 下がるのに、私たちでは出ないため。`#rhf` の `.rhf-frame` は `display:none` で、AUI の
  `RecentHistoryFooterJS`（`build/p081/js/30.js`）が scroll の位置を 200 ms ごとに見て枠を出し、`/hz/rhf` への ajax が失敗すると
  `#rhf-error` を出す。
- 私たちでは AUI の module が一つも解決しない: 頁に probe の script を足して調べると `P.when('A')`・`P.when('jQuery')`・
  `P.when('ready')` の callback が走らず、`P.now('A')` は undefined。
- 原因: **script が `createElement('script')` で作って挿入した `<script src>` を私たちは走らせない**（`page/script.c` は parser の
  hook だけ）。Amazon の top の外の script 43 本のうち頁の `<script src>` は 2 本だけで、残り（AUI の core・jQuery・card・carousel・
  RHF 等）は `P.load.js` が動的に挿入する。小さな頁で確かめた: 動的な `<script src>` は私たちでは走らず `onload` も来ない
  （Chromium は "dyn ran"・"onload"）。inline の動的な script は走る。
- p077〜p083 の Uncaught の数は、inline の script と頁の 2 本だけのもの。動的な script が走ると AUI の 6 MB の code が初めて走り、
  新しい Uncaught が出る見込み。

### 2. layout の差（script なしでも出る、0〜650 の帯の 12〜28%）

- 「間もなく終了のセール」の card（top）: 4 つの tile の grid（`grid-template-areas`、`grid-template-rows: repeat(2,1fr)`）が
  card の高さ一杯に伸びず、tile の画像（`height:100%`・`object-fit:contain`）が切れる。card 自体も行の高さに伸びない
  （`height:100%` の flex column の card が grid の行の中）。
- header の 2 段目（`#nav-main`）: 左端の「☰ すべて」が「Amazonポイント」に重なる（hamburger の icon と文字）。
- 検索の結果: 商品の画像の大きさ（私たちは大きく、灰色の背景の枠が無い）、sponsored brand の logo の画像（KIMIE）が出ない、
  左の filter の列の行の高さ（下に行くほどずれる）。
- 文字: 太字の見出しの幅が違い折り返しが変わる（「間もなく終了のセー／ル」）。

### 3. 原因の特定（getComputedStyle と getBoundingClientRect の probe を頁の末尾に足し、Chromium と同じ probe を比べた）

- **百分率の高さが解決されない（layout 全体）**: `layout/block.c` は `height`・`min-height`・`max-height` の px だけを見て、
  `%` は auto として扱う。小さな頁で確かめた: `height:300px` の `ul` の中の float の `li`（`height:100%`）は Chromium 300 px・
  私たち 20 px（内容の高さ）、`height:200px` の中の inline-block（`height:100%`）は Chromium 200・私たち 0。
  - top の card: `li.gwm-window-tile`（`float:left; height:100%`、`ul#gwm-window` は 489.6 px）の中の card の `height:100%` の
    連鎖が、私たちでは内容の高さ（200 px）になる（Chromium 489.6 px）。その中の grid（`grid-template-rows: repeat(2,1fr)`）の行も
    高さを持たず、tile の画像（`height:100%`・`object-fit:contain`）が自然の高さ（745 px）になって切られる。
  - search の結果の画像: `img.s-image`（`position:absolute; max-width:100%; max-height:100%`、224 px の正方形の枠）の
    `max-height:100%` が効かず、Chromium の 159x224 に対し私たちは 224x224（縦横比が崩れ、左右に広がる）。
- **flex item の自動の最小の大きさ（直した）**: `min-width` の初期値が px の 0 で、flex の row の item が内容より狭く縮んでいた。
  header の 2 段目の「☰ すべて」（`#nav-hamburger-menu`、`display:flex`）が 21.7 px に縮み、icon の幅が 0、文字が隣の
  「Amazonポイント」に重なっていた（Chromium 80.5 px）。
- **太字の文字の幅**: Latin の太字が Chromium より広い（"Amazon Basics" 14px bold: 私たち 114.3、Chromium 104.8。日本語の太字は
  42 と 45）。見出しの折り返しが変わる（top の「間もなく終了のセー／ル」）。通常の太さの文字の幅は一致した（fallback の font を
  渡したとき）。

## この Phase で直したもの

- `css/cascade.c`: `min-width`・`min-height` の初期値を `auto`（`CSS_UNIT_AUTO`、layout では 0 と同じ）にした。
- `layout/flex.c`: row の item の自動の最小の大きさ（CSS Flexbox 4.5）: `min-width:auto` で内容が scroll しない item は、line が
  縮む時だけ内容の min-content の幅（`width` が px でより狭ければそれ、`max-width` の内）より狭くならない（`flex_auto_minimums`・
  `flex_min_content`）。縮みは 9.7 のように最小で止まった item を凍らせて残りで分け直す（`flex_shrink`、前は一度だけ縮めて最小で
  切っていたので合計が溢れた）。column の item は前と同じ（0 まで縮む）。
- 試験: `plan/ws074/tests/dom/flexmin.html`（5 行、新）と Chromium 153 の expected（内容の幅、`min-width:0`、`overflow:hidden`、
  px の width、max-width、computed の `auto`）。
- 効果: header の hamburger（「☰ すべて」）が Chromium と同じ形になった（21.7 px → 70 px 前後、Chromium 80.5 px。差は太字の幅）。
  画素: top-local 64.47 → 64.49%、search-local 76.04 → 76.02%、top-local-noscript 79.80 → 79.84%、search-local-noscript 77.16 → 77.15%
  （ほぼ不変: 2 段目の項目が Chromium と同じ位置に並んだが、Chromium が隠す最後の項目「新着商品」が私たちでは出る）。

## 確認（host は Debian の cc と Chromium 153）

- host の build（plain と ASan、-Werror）warning 0。style-check: `layout/flex.c` に指摘 0（`css/cascade.c` の 1 件は前から）。
- 回帰（plain、最後の変更（凍らせる縮み）の後）: golden 76/76、host-view 59、host-form 28、host-link 22、host-position 19、host-text 20、
  host-relayout 161、host-base 2038・heap 31・interp 45・number 71・object 98（全て 0 failed）、run-dom-tests **19/19**（flexmin を追加）、
  run-js-tests 13/13、loader 11/11、http 14/14・`--async` 17/17、font 8/8、html5lib tree 1854/1959・fragment 206/206。
- ASan の回帰: 自動の最小の大きさを入れた時点（凍らせる縮みの前）で plain と同じ結果。凍らせる縮みの後の ASan は**未実施**。
- guest（QEMU）の run-dom-tests・boot test: **未実施**（wrap up で停止）。実機: 未実施。

## Phase の案（効果の大きい順、見込み）

| 順 | 案 | 内容 | 効果の見込み | 大きさ |
| --- | --- | --- | --- | --- |
| 1 | 動的に挿入された script（bind/・page/・dom/） | script が挿入した `<script src>` を取得して走らせ、`load`・`error` の event を出す（inline の動的な script も spec どおり）。「already started」の flag（parser と innerHTML の断片の script は started）。外の script は task（0 ms の timer）で走らせる | top の約 10〜13 点（AUI の core・jQuery・card・carousel・RHF が初めて走る。RHF の枠（`/hz/rhf` の ajax の失敗で `#rhf-error`）が出れば footer の位置が合う）。ただし AUI の 6 MB の code が初めて走り、新しい Uncaught と未実装の API（XMLHttpRequest（p064）、MutationObserver ほか）が出る見込み | 中（bind の挿入の口と page の取得・実行、試験の頁） |
| 2 | 百分率の高さ（layout/） | `height`・`min-height`・`max-height` の `%` を、高さの定まった containing block（px、定まった % の連鎖、root は viewport、stretch された flex・grid の item、absolute の containing block）に対して解決。block・float・inline-block・absolute・replaced（max-height）と grid の `1fr` の行の高さ | top の card（`height:100%` の連鎖と 4 つの tile の grid、画像の `height:100%`）と search の結果の画像（`max-height:100%` の縦横比）: 各 2〜6 点の見込み | 中〜大（高さの「定まり」を layout 全体に渡す） |
| 3 | 太字の文字の幅（text/） | 合成の太字（fake bold）で advance を広げない（Chromium の Skia と同じ）か、太字の face を使う。Latin の太字が約 9% 広い | 見出しの折り返し（top の card の見出し）、header の文字の位置。1 点前後 | 小 |
| 4 | flex の小さな残り（layout/） | 隠れる overflow の項目（`#nav-xshop` の最後の項目を Chromium は `overflow:hidden` の中で切る）、flex item の display の blockification（computed の値）、column の自動の最小の大きさ | 0.5 点前後 | 小 |
| 5 | CSSOM の小さな不足（bind/） | `cssFloat`（`float` の accessor）、shorthand の computed の値 | 画素には効かない（script の分岐） | 小 |

- 参考: search は script 付きと script なしの差が小さい（76.04 と 77.16）ので、search の差の大部分は案 2（画像）と文字・filter の
  列の位置（行の高さは一致、太字の幅と画像の大きさでずれる）。sponsored brand の logo の画像（KIMIE）が出ない件は未調査。

## Resume point

- 2026-09-29（wrap up）: 調べと Phase の案は済み。この Phase を cleared にするには、flex の変更（最後の commit）について ASan の回帰、
  guest（`build-browser-image.sh`・run-dom-tests `--outputs`）、boot test を流す。案 1〜5 は main が計画する（Phase の番号は main と合わせる）。
- 調べの道具: 頁の末尾に getComputedStyle・getBoundingClientRect を console に出す probe の script を足し、私たちの `--run` と
  Chromium の `--dump-dom`（`--hide-scrollbars --window-size=1280,900`）の console を比べる（p082 の getComputedStyle で可能に
  なった。probe の頁は build/ の下に作り、終わったら消した）。
