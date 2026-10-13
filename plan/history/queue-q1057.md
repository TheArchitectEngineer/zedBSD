<!-- awesome-plan project=zedbsd record=q1057 -->

# q1057 / WS074 vertical writing-mode logical margins

Status: finished（2026-10-14 03:01–03:05 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p205の有限scope](../ws074/phase205/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1057-i01 | [p205](../ws074/phase205/phase.md) whole | 4論理margin longhand/2 shorthandをcomputed flow後に物理側へ写像。固定gap5・Flex10とhost/CSS/build/Acid回帰。WPT/Master/公開ABI変更なし | cleared。p205 whole cleared |

Outcome: 固定vertical-lr gap5 reftestは5/5 local pixel exact PASS。host CSS rule51/51・CSSOM12/12・mutation12/12、warning0 build、固定Flex10/10 PASS、Acid3 score100/100・pixel差0、Acid2差1865。[証拠](../ws074/phase205/evidence/20261014-q1057/README.md)。旧PASS→FAIL8件中5件を対象で回復。

Remaining: 旧8件中3件、p190の他の画素FAIL/runner未対応、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
