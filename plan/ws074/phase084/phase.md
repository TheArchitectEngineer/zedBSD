<!-- awesome-plan project=zedbsd record=ws074p084 -->

# ws074-p084: Amazon の画素の差の原因の調べ（DOM・layout）

Phase ID: `ws074-p084`
Parent: [WS074](../ws.md)
Status: in-progress
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
- `layout/flex.c`: row の item の自動の最小の大きさ（CSS Flexbox 4.5）: `min-width:auto` で内容が scroll しない item は、
  line が縮む時だけ内容の min-content の幅（`width` が px でより狭ければそれ、`max-width` の内）より狭くならない。
  column の item は前と同じ（0 まで縮む）。
- 効果: header の hamburger が Chromium と同じ形（80 px 前後、icon と文字が並ぶ）。画素は top-local 64.47 → 64.29%、search-local
  76.04 → 75.85%（hamburger が広がり 2 段目の項目が右へずれ、Chromium が隠す最後の項目（「新着商品」）が私たちでは出るため）。
- 確かめ: golden 76/76（変わらず）。他の回帰は下の「確認」。

## Resume point

- 2026-09-29: 原因の特定まで済み、flex の自動の最小の大きさを直した。残り: 回帰（plain・ASan）、Phase の案の表、記録。
