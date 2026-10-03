<!-- awesome-plan project=zedbsd record=ws074p075 -->

# ws074-p075: container query の単位（cqi・cqw・cqh・cqb）

Phase ID: `ws074-p075`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p061
由来: 2026-09-29 main の判断（Amazon のトップの card が `container-type: inline-size` の中で `height: 143cqi` 等を使う。p061 は
cq の単位を vw として扱っていた）。順は p074 の後、p037 の前。

## 範囲

`container-type`（normal・inline-size・size）と `container` の shorthand（`name / type` の type だけ）、`cqw`・`cqi`・`cqmin`・`cqmax`
（container の幅）、`cqh`・`cqb`（size の container の高さ、無ければ viewport）、calc の中の cq。`@container` の規則は Amazon の
card の配置に要らなかったので入れていない。

## 設計と実装

- CSS（`css/css.h`・`internal.h`・`values.c`・`cascade.c`）: `CSS_PROP_CONTAINER_TYPE`、単位 `CSS_DUNIT_CQW`・`CQH`、calc の sum の
  `cqw`・`cqh`。cascade は cq の長さを、styling 中の要素の祖先で style（engine の style の cache）が container の最も近いものの
  content box の大きさで px にする。大きさは page の callback（`css_engine_set_container_lookup`）が前の layout から答える。
  答えられない container は miss として数える（`css_engine_container_missed`）。使った container と大きさを記録する
  （`css_engine_container_uses`・`_use`）。container が無ければ viewport（仕様の small viewport と同じ）。
- page（`page/page.c`・`page.h`）: layout の間、前の layout（`previous_layout`）を保ち、callback はその box の content box を返す。
  layout の後、miss があったか、記録した container の大きさが新しい layout と 0.5 px を超えて違えば、その layout を「前」にして
  style の cache を捨て（`css_engine_forget_styles`）、もう一度 layout する（2 回まで）。最初の layout（前が無い）と、sheet の
  到着や viewport の変化で container の大きさが変わった時に、cq が新しい大きさになる。
- 試験: `tests/pages/container.html`（新、golden 4 つ）: 300px・500px の container の 50cqi・40cqi の高さ・`calc(10px + 25cqw)`・
  `max(20px, 5cqi)`、grid の `25cqi 1fr`、size の container の cqh、shorthand の `container: card / inline-size`、入れ子の container、
  container 無し（viewport）。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: 新しい指摘 0（既存の 1 件）。
- 回帰（plain と ASan）: golden 72/72（container の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、host-link 22/22、
  host-position 19/19、host-text 20/20、host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8、host-relayout 154/154（最初の実装は viewport を
  変えた後の layout が新しい page と違い 1 件 FAIL、container の大きさの照合を入れて通った）。ASan の `--render` で Amazon の 3 つの
  capture に報告なし。
- `chrome-boxes.py container.html`（800x600）: 19/19 の box が 1px 以内。`render-compare.py`: 99.94%。
- Amazon（2026-09-28 23:52 の capture、1280x900）: トップ 画素 79.12% → **80.40%**、ink 75.06% → **76.55%**。検索 77.16%（不変）。
  `--render` は host で検索 0.89 s、トップ 0.61 s（2 回目の layout を含む）。
- guest（QEMU の Venus）: `browser-page.sh container.html` status 0。live の `https://www.amazon.co.jp/`（取得 2 回）: 照合を入れる前は
  card の文字が巨大（sheet が届く前の全幅の container の大きさの style が cache に残った、`…-before-verify.png`）、照合の後は正しい
  大きさ（写真）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p075-20260929-container.png`、`p075-20260929-amazon-top-local-noscript.png`・`…-search-…`。
- guest（QEMU）: `p075-20260929-guest-container.png`、`p075-20260929-guest-amazon-top-30s.png`、`…-30s-before-verify.png`。

## 未実施・残り

- 実機は未実施。
- `@container` の規則、container の名前、font-size の cq（cascade の時の前の layout の大きさ、照合で直る）、cqh の inline-size の
  container（viewport を使う）、3 回以上の layout が要る入れ子（2 回で止める）。
- トップの 2 番目・3 番目の card の中の商品の格子と card の背景の高さ（Chromium と違う。原因は未調査）。
