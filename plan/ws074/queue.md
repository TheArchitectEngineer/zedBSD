<!-- awesome-plan project=zedbsd record=queue -->

# q1075 / WS074 CSS Grid fit-content min-content floor

Status: finished（2026-10-14 05:46–05:54 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p214有限scope](phase214/phase.md)に適用。
Timebox: 2026-10-14 05:46 JST開始、最大90分（07:16 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1075-i01 | [ws074-p214](phase214/phase.md) | Grid横書き非置換auto幅非stretchのfit-content min-content下限を既存内部測定と共有。代表WPT・局所fixture・固定静的Grid旧PASS・host/Acid確認。WPT/runner/browser ABI/Master不変更 | cleared | p213 q1073 auto幅・p189 q1074 baseline |

Dependency graph: p173 manifest → p213 q1073 auto幅 → p189 q1074 baseline → p214 q1075 min-content下限 → p173全件100%判断。

Outcome: q1075-i01/p214 whole cleared。[証拠](phase214/evidence/20261014-q1075/README.md)。固定幅変化WPT14 ID旧6 PASS/8 FAIL→10 PASS/4 FAIL、静的Grid529旧124 PASS維持、局所47/47、Acid3差0。p173全3,805件100%未達。

Upcoming Work Outlook: Grid427 FAILとmodule script no_output32件から共通原因を別の有限Queueで切り分ける。全域baseline testharness223 PASS/1,232非PASS（q1068）、環境未対応328、reftest未対応413・静的899 pixel FAIL。Gridのみq1072で43/502 PASS、全域の数へは合算しない。共有 `plan/queue.md` / Master / GitHub publication はQ1セッションとの競合を避けpending projection。
