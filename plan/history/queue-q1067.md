<!-- awesome-plan project=zedbsd record=q1067 -->

# q1067 / WS074 CSS Grid WPT process crash

Status: finished（2026-10-14 04:08–04:13 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p210の有限scope](../ws074/phase210/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1067-i01 | [p210](../ws074/phase210/phase.md) whole | 固定Grid SIGSEGVのASan調査・GC一般修正、WPT/host/build/Acid回帰。WPT/runner/ABI/Master変更なし | cleared。p210 whole cleared |

Outcome: 旧通常3/3 SIGSEGV、pre-fix ASan heap-buffer-overflow。登録済みlarge cell照合後、通常3/3・ASan1/1 completion。host-heap31/31、Acid3差0。[証拠](../ws074/phase210/evidence/20261014-q1067/README.md)。

Remaining: Grid WPT test assertion FAIL、p189最新同条件再計測、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
