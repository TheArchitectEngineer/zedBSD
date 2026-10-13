<!-- awesome-plan project=zedbsd record=q1071 -->

# q1071 / WS074 CSS Grid horizontal RTL alignment

Status: finished（2026-10-14 04:59–05:09 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p212の有限scopeと記録済み旧PASS対策](../ws074/phase212/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1071-i01 | [p212](../ws074/phase212/phase.md) | RTL Grid明示列・明示幅in-flow項目の基本配置、旧PASSを破るauto列端数の一般修正。固定WPT18 subtest、関連90・全Grid529静的、host/build/Acid。WPT/runner/ABI/Master変更なし | cleared。p212 whole cleared |

Outcome: 代表WPT9/18→18/18 subtest PASS、whole 1 ID FAIL→PASS。中間版90静的は旧PASS1件217差画素、auto列端数修正後の最終90件は旧18 PASS/72 FAIL、全Grid529件は旧123 PASS/406 FAILを維持。host配置33/33・CSS rule51/51・CSSOM12/12、warning0 build、Acid3 100/100・画素差0、Acid2差1865。[証拠](../ws074/phase212/evidence/20261014-q1071/README.md)。

Remaining: 固定testharness全1,455 IDの最新同条件集計はq1068の223 PASS/1,232非PASS・環境未対応328で、q1071の1 ID改善は次の再計測まで全域へ合算しない。p190固定静的1,561件はq1062の662 PASS/899 FAIL、未対応413。p189/p190/p173全3,805件100%未達。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
