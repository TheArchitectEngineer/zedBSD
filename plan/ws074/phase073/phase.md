<!-- awesome-plan project=zedbsd record=ws074p073 -->

# ws074-p073: direction の最小（rtl の flex row と text-align: start）

Phase ID: `ws074-p073`
Parent: [WS074](../ws.md)
Status: cleared（2026-09-29）
Phase disposition: normal
Queue: なし（サブエージェントが worktree の branch で実行）
依存: p035
由来: 2026-09-29 main の判断（サブエージェントの提案: Amazon の検索の page の本体の行 `.s-desktop-content.sg-row` は
`direction: rtl` の flex row で、子は `ltr`。Chromium では filter の列が左・結果が右、私たちは逆だった。p060 の phase.md）。
順は「p073 → p072 → p037」。

## 範囲

`direction`（ltr・rtl、継承）と HTML の `dir` 属性、rtl の flex の row（右から左へ）、`text-align: start`・`end` の rtl、rtl の
block の中の auto でない margin の box（右に寄る）。bidi の並べ替え（文字の順）は範囲の外。

## 実装

- CSS（`css/css.h`・`internal.h`・`values.c`・`cascade.c`・`ua.c`）: `direction`（`CSS_PROP_DIRECTION`、継承、anonymous block にも）、
  `text-align: end` を `CSS_TEXT_ALIGN_END` に（前は right）、UA の sheet に `[dir=rtl i] { direction: rtl }`・`[dir=ltr i]`。
- flex（`layout/flex.c`）: rtl の container の row は主軸の向きが逆（row-reverse はさらに逆で左から）。justify-content の start は右。
- 行（`layout/inline.c`）: `text-align` の start（と justify）は rtl で右、end は ltr で右・rtl で左。
- block（`layout/block.c`）: rtl の block の中の、auto の margin の無い大きさの決まった box は左の margin が残りを取り、右に寄る
  （float・inline-block・out of flow・flex item は除く）。
- 試験: `tests/pages/direction.html`（新、golden 4 つ）: rtl の flex row（2 列、伸びる item、flex-end、row-reverse、gap）、rtl・ltr の
  start・end、rtl の block の中の 300px の block、`dir` 属性、入れ子の ltr の flex。

## 確認（host は Debian の cc。guest は QEMU（Venus）。実機は未実施）

- host の build（plain と ASan、-Werror）warning 0。style-check: 新しい指摘 0（既存の 1 件）。
- 回帰（plain と ASan）: golden 60/60（direction の 4 つを追加、他は不変）、host-view 59/59、host-form 28/28、host-link 22/22、
  host-position 19/19、host-text 20/20、host-base・heap・interp・number・object 全て 0 failed、run-js-tests 7/7、run-dom-tests 6/6、
  run-loader-tests 11/11、run-http-tests 14/14・`--async` 17/17、run-font-tests 8/8、host-relayout 133/133。ASan の `--render` で
  Amazon の 3 つの capture に報告なし。
- `chrome-boxes.py direction.html`（800x500）: 30 のうち 28 が 1px 以内（違う 2 つは「row-reverse」の文字の幅 1.2 px）。
  `render-compare.py`: 98.06%。
- Amazon（2026-09-28 23:52 の capture の `*-local-noscript.html`、1280x900）:

  | page | p071 の後 | p073 の後 |
  | --- | --- | --- |
  | トップ | 画素 34.16%、ink 24.28% | 画素 34.16%、ink 24.28% |
  | 検索 | 画素 68.83%、ink 20.06% | **画素 77.16%、ink 34.68%** |

  検索の filter の列が左、結果の格子が右になり Chromium と同じ並び。`--render` は host で検索 0.92 s、トップ 0.61 s。
- guest（QEMU の Venus、zdesktop 1280x800）: `browser-page.sh direction.html` status 0。live の `https://www.amazon.co.jp/s?k=kei`
  （取得 1 回）: 30 s で header・左の filter・右の結果が Chromium と同じ並び（写真）。

## 写真（`/home/awe/zedBSD-rpi4/build/ws074-shots/`）

- host: `p073-20260929-direction.png`（私たち | Chromium | 違い）、`p073-20260929-amazon-search-local-noscript.png`・`…-top-…`。
- guest（QEMU）: `p073-20260929-guest-direction.png`、`p073-20260929-guest-amazon-search-30s.png`。

## 未実施・残り

- 実機は未実施。
- bidi（文字の並べ替え、アラビア語・ヘブライ語）、rtl の inline の box の並び、rtl の float・absolute の既定の位置、rtl の
  flex の column の交差軸（start が右）、rtl の block formatting context の scrollbar の側、`unicode-bidi`、論理 property の rtl。
