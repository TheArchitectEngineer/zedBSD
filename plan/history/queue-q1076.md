<!-- awesome-plan project=zedbsd record=q1076 -->

# q1076 / WS074 CSS Grid replaced item normal/stretch

Status: finished（2026-10-14 05:55–06:22 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p215有限scope](../ws074/phase215/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1076-i01 | [p215](../ws074/phase215/phase.md) | CSS normal/stretch分離、Grid replaced画像の自然幅/明示stretch幅と比率高さ、Flex normal従来動作保持。固定WPT14・局所・静的Grid529旧PASS・Flex10・host/Acid確認。WPT/runner/browser ABI/Master不変更 | cleared / p215 whole cleared |

Outcome: 固定幅変化14 WPTは旧10 PASS/4 FAIL→**14/14 PASS**、4 FAIL→PASS・旧PASS後退0。[証拠](../ws074/phase215/evidence/20261014-q1076/README.md)。静的Grid529旧124 PASS/405 FAILとFlex10/10を維持、局所画像7/7、Acid3画素差0。Grid項目のnormal/stretch区別は[CSS Grid L1 §6.2](https://www.w3.org/TR/css-grid-1/#grid-item-sizing)に基づく。

Remaining: Grid全502 testharness最新baselineはq1074の57 PASS/413 FAIL/32 no_output、静的Grid405 FAIL、p189/p190/p173全3,805件100%未達。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
