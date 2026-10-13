<!-- awesome-plan project=zedbsd record=queue -->

# q1067 / WS074 CSS Grid WPT process crash

Status: finished（2026-10-14 04:08–04:13 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p210 q1067有限scope](phase210/phase.md)に適用。
Timebox: 2026-10-14 04:08 JST開始、最大90分（05:38 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1067-i01 | [ws074-p210](phase210/phase.md) whole | 固定Grid1 ID exit-11のASan/デバッガ調査・一般最小修正、completion・host・warning0 build・Acid確認。WPT/runner/ABI/Master変更なし | cleared | p189 q1066再現SIGSEGV |

Dependency graph: p173 manifest → p189 q1066再現 → p210一般修正 → p189同条件再測定 → p173全件100%判断。

Outcome: q1067-i01 / p210 whole cleared。pre-fix ASanのheap-buffer-overflowをGC large登録照合で修正、固定Grid WPTは旧通常3/3 SIGSEGV→通常3/3・ASan1/1 completion。host-heap31/31・plain/ASan warning0 build・Acid3差0。[証拠](phase210/evidence/20261014-q1067/README.md)。Grid WPT testはFAIL、p173全3,805件100%未達。

Upcoming Work Outlook: testharness残非PASS/環境未対応328、reftest未対応413・静的899 pixel FAILを後続有限Queueで扱う。共有 `plan/queue.md` / Master / GitHub publication はQ1セッションとの競合を避けpending projection。
