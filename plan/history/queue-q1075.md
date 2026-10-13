<!-- awesome-plan project=zedbsd record=q1075 -->

# q1075 / WS074 CSS Grid fit-content min-content floor

Status: finished（2026-10-14 05:46–05:54 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p214有限scope](../ws074/phase214/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1075-i01 | [p214](../ws074/phase214/phase.md) | 横書きGrid非置換auto幅非stretchのmin-content下限を既存内部測定と共有。固定代表/系列WPT、局所fixture、静的529旧PASS、host/Acid検証。WPT/runner/browser ABI/Master不変更 | cleared / p214 whole cleared |

Outcome: 固定幅変化14 testharness IDは6 PASS/8 FAIL→**10 PASS/4 FAIL**、4 FAIL→PASS/旧PASS後退0。[証拠](../ws074/phase214/evidence/20261014-q1075/README.md)。静的Grid529は旧124 PASS/405 FAILを維持、局所47/47、host warning0、Acid3画素差0。最後の4 FAILはreplaced image固有で今回scope外。

Remaining: Grid全502 testharness最新baselineはq1074の57 PASS/413 FAIL/32 no_output、静的Grid405 FAIL。p189/p190/p173全3,805件100%未達。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
