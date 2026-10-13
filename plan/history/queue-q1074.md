<!-- awesome-plan project=zedbsd record=q1074 -->

# q1074 / WS074 CSS Grid testharness recensus

Status: finished（2026-10-14 05:38–05:44 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p189の有限部分scope](../ws074/phase189/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1074-i01 | [p189](../ws074/phase189/phase.md) partial | 固定Grid適格testharness502 IDをq1072同条件で最大128件shard再測定し、全ID/outcome/subtest/checkpoint/rawと残失敗群を照合。runner/browser/WPT/ABI/Master変更なし | cleared。p189 whole uncleared |

Outcome: 旧Grid502 ID 43 PASS/427 FAIL/32 no_output→**57 PASS/413 FAIL/32 no_output**、14 whole FAIL→PASS/0 PASS→FAIL、subtest PASS1809→1851。[証拠](../ws074/phase189/evidence/20261014-q1074/README.md)。4 shard exit0、ID/hash/checkpoint/gzip raw全502一致。残413 FAILのうちalignment配下220、module script関連no_output32。

Remaining: p189全1,783件・Grid413 FAIL/32 no_output、p190静的899のq1062時点FAIL・未対応413、p173全3,805件100%。全1,455適格testharnessの同条件全域baselineはq1068の223 PASS/1,232非PASSで、Grid subsetを二重計上しない。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
