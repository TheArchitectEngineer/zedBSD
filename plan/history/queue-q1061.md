<!-- awesome-plan project=zedbsd record=q1061 -->

# q1061 / WS074 Flex base versus hypothetical size

Status: finished（2026-10-14 03:24–03:29 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p208の有限scope](../ws074/phase208/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1061-i01 | [p208](../ws074/phase208/phase.md) whole | Flex base/hypothetical分離とwrap/min/max freeze修正。固定回帰1/textarea3/Flex10とhost/build/Acid回帰。WPT/runner/ABI/Master変更なし | cleared。p208 whole cleared |

Outcome: q1060新規FAIL1件は5000差画素→0。guard3件と合わせ4/4、固定Flex10/10 local pixel exact PASS。host physical21/21・CSS rule51/51・CSSOM12/12、warning0 build、Acid3差0、Acid2差1865。[証拠](../ws074/phase208/evidence/20261014-q1061/README.md)。q1060全件snapshotは保持、広域再集計は次Queue。

Remaining: p190他pixel FAIL/runner未対応、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
