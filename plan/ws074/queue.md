<!-- awesome-plan project=zedbsd record=queue -->

# q1076 / WS074 CSS Grid replaced item normal/stretch sizing

Status: finished（2026-10-14 05:55–06:22 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p215有限scope](phase215/phase.md)に適用。
Timebox: 2026-10-14 05:55 JST開始、最大90分（07:25 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1076-i01 | [ws074-p215](phase215/phase.md) | CSS normal/stretch分離、Grid横書きreplaced image自然幅・明示stretch幅・比率高さ、Flex normal既存動作維持。固定WPT4/系列14・局所・静的Grid529旧PASS・Flex10・host/Acid検証。WPT/runner/browser ABI/Master不変更 | cleared | p214 q1075 min-content・p189 q1074 baseline |

Dependency graph: p173 manifest → p189 q1074 baseline → p214 q1075 min-content下限 → p215 q1076 replaced/normal → p173全件100%判断。

Outcome: q1076-i01/p215 whole cleared。[証拠](phase215/evidence/20261014-q1076/README.md)。固定幅変化系列14 WPTは旧10 PASS/4 FAIL→14/14 PASS、静的Grid529旧124 PASS全維持・Flex10/10、局所画像7/7・Acid3差0。p173全3,805件100%未達。

Upcoming Work Outlook: Grid427 FAILとmodule script no_output32件から共通原因を別の有限Queueで切り分ける。全域baseline testharness223 PASS/1,232非PASS（q1068）、環境未対応328、reftest未対応413・静的899 pixel FAIL。Gridのみq1072で43/502 PASS、全域の数へは合算しない。共有 `plan/queue.md` / Master / GitHub publication はQ1セッションとの競合を避けpending projection。
