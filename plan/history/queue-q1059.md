<!-- awesome-plan project=zedbsd record=q1059 -->

# q1059 / WS074 fractional textarea border rasterization

Status: finished（2026-10-14 03:13–03:18 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p207の有限scope](../ws074/phase207/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1059-i01 | [p207](../ws074/phase207/phase.md) whole | 縦textarea残1件198差画素を小数幾何・border/文字で切り分け、一般原因のみ最小修正、固定3/Flex10とhost/build/Acid回帰。WPT/runner/ABI/Master変更なし | cleared。p207 wholeとp206後続whole cleared |

Outcome: font shorthand line-height resetで198→110差画素、textarea描画端のpixel丸めで差0。旧8回帰の固定3件3/3 local pixel exact PASS、固定Flex10/10 PASS。host CSS rule51/51・CSSOM12/12・submit13/13、warning0 build、Acid3差0、Acid2差1865。[証拠](../ws074/phase207/evidence/20261014-q1059/README.md)。host-formの入力欄paint座標2件の既存失敗はfont修正を外しても同じ結果、今回追加悪化なし。

Remaining: p190他画素FAIL/runner未対応、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
