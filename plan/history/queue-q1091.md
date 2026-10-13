<!-- awesome-plan project=zedbsd record=q1091 -->

# q1091 / WS074 horizontal RTL Grid absolute start

Status: finished（2026-10-14 07:42–07:48 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1091-i01 | [p217](../ws074/phase217/phase.md) partial | 水平RTL `justify-content:normal`の固定track配置、直接absolute子のvalid definite area/auto inset/inline startで幅後右端配置。最小HTML・固定WPT2+旧PASS・Grid/JS/build/Acid回帰。縦書き/全Grid/runner/WPT/ABI/Master対象外 | cleared。p217 whole uncleared |

Outcome: RTL最小5ケースとLTR回帰PASS。固定WPT代表2件は水平RTL subtest各FAIL→PASS、wholeは縦書き2件FAIL。旧PASS代表100/100、Grid48/48、physical21/21、classic JS14/14、warning0 build、Acid3差0・Acid2差1865。[証拠](../ws074/phase217/evidence/20261014-q1091/README.md)。

Resume: 縦書きGrid absolute alignmentを調査し一般欠陥を修正、Grid502全再測定。p189/p173全件100%未達。共有Queue/Master/GitHub publicationはQ1との競合を避けpending projection。pushなし。
