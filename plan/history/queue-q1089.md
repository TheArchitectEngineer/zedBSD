<!-- awesome-plan project=zedbsd record=q1089 -->

# q1089 / WS074 definite abspos Grid static origin

Status: finished（2026-10-14 07:35–07:39 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1089-i01 | [p217](../ws074/phase217/phase.md) partial | 水平LTRの有効明示Grid areaを持つabsolute子のstatic原点。明示inset/無効line/in-flow保持、最小host・固定WPT代表・classic JS/build/Acid回帰。RTL/縦書き/全Grid/公開ABI/Master変更なし | cleared。p217 whole uncleared |

Outcome: 最小hostでarea子(108,88)、明示inset(15,17)、無効line(8,8)。固定WPT代表2件は水平subtest各FAIL→PASS、wholeはRTL/縦書きでFAIL継続。旧PASS module代表維持。[証拠](../ws074/phase217/evidence/20261014-q1089/README.md)。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。

Resume: RTLと縦書きのGrid absolute静的位置・軸対応、他alignment失敗とGrid全件再測定。p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
