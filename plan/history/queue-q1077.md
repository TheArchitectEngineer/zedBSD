<!-- awesome-plan project=zedbsd record=q1077 -->

# q1077 / WS074 CSS Grid testharness recensus

Status: finished（2026-10-14 06:23–06:28 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p189有限部分scope](../ws074/phase189/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1077-i01 | [p189](../ws074/phase189/phase.md) partial | 固定Grid適格testharness502 IDをq1074同条件で最大128件shard再測定し、全ID/outcome/subtest/checkpoint/rawと残失敗群を照合。runner/browser/WPT/ABI/Master変更なし | cleared。p189 whole uncleared |

Outcome: 旧Grid502 ID 57 PASS/413 FAIL/32 no_output→**71 PASS/399 FAIL/32 no_output**、14 whole FAIL→PASS/0 PASS→FAIL、subtest PASS1851→1877。[証拠](../ws074/phase189/evidence/20261014-q1077/README.md)。4 shard exit0、ID/hash/checkpoint/gzip raw全502一致。残399 FAILのうちalignment配下206、module script関連no_output32。

Remaining: p189全1,783件・Grid399 FAIL/32 no_output、p190静的899のq1062時点FAIL・未対応413、p173全3,805件100%。全1,455適格testharnessの同条件全域baselineはq1068の223 PASS/1,232非PASSで、Grid subsetを二重計上しない。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
