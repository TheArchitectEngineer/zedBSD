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

## Resume point

- 2026-09-29: 調べの途中（上）。残り: 項目 2 の各々の原因の CSS の特定と面積の見積り、Phase の案の表、小さく直せるものの修正。
