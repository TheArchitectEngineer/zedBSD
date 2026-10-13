<!-- awesome-plan project=zedbsd record=queue -->

# q1035 / WS074 first summary UA display

Status: finished（2026-10-14 01:26–01:27 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: 2026-10-14 00:07 JST、この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を、[p193のq1035有限部分scope](phase193/phase.md) に適用。
Timebox: 2026-10-14 01:26 JST 開始、最大90分（02:56 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1035-i01 | [ws074-p193](phase193/phase.md) partial | HTML UA CSSに `details > summary:first-of-type { display:list-item }` を追加。固定details-display WPT8 subtestとauthor override、host build/CSS回帰、Acid3/2差画素非増加を確認。WPT/Master/公開ABI変更なし。最大90分 | cleared（partial） | q1032 details-display 7/8 |

Dependency graph: p173固定manifest → p189 q1024 shard → p192 q1025一意id output → p193 q1026 details.open → q1027-i01 → p193 whole判断 → p173最終100%判断。p189 whole/p190/p192 wholeは別の必要出力、p191 whole cleared。

Upcoming Work Outlook: p191の残るtextdecoration CSSOM、p189 runnerの環境対応、[p190](phase190/phase.md) reftest/crashtest coverage、p173全件baselineを候補とする。次の Queue も有限scopeを記録する。

Coordination: [q1014](../history/queue-q1014.md) までの履歴を保持。共有 `plan/queue.md` / Master / GitHub publication は別の Q1 セッションとの競合を避け pending projection。WS074 lane の状態はこの file が所有。

Previous outcome: q1013-i01 partial cleared、p189 whole uncleared。選定testharness1件のWPT callbackから12 subtestとcompletionを取得し、4 PASS/8 FAIL、harness OK、runnerの結果はFAIL。status分類の unit5/5、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1013/README.md)。残り1,782 testharnessと p173全件baseline、100%は未達。

Outcome: q1014-i01 partial cleared、p189 whole uncleared。19領域から条件を満たす15 IDを選定・実行、12 test FAIL・1 harness ERROR・1 completion無し・1 stdout無し、PASS0。4領域は未実行理由を保存。選定 unit2/2、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1014/README.md)。q1013 と合わせユニーク16/1,783 testharnessを試行。ブラウザ/WPT/Master無変更、Acid3再計測省略。p173全件baseline・100%未達、共有Master/Queue/GitHub publication pending。

Outcome: q1015-i01 partial cleared、p189 whole uncleared。textdecorationの全16 IDを実測、4 PASS/12 FAIL、subtest47 PASS/132 FAIL。全16 completion取得。`CSS` global欠落とtext-decoration CSSOM不足を共通原因候補として記録。[証拠](phase189/evidence/20261014-q1015/README.md)。browser/WPT/Master無変更、p173全件baseline・100%未達。

Outcome: q1016-i01 partial cleared、p191 whole uncleared。`CSS.supports` を宣言parserへ接続。host fixture、build warning0、CSSOM12/12・rule model44/44、固定16件、Acid3差0を検証。textdecoration4 PASS/12 FAILと数不変だが `CSS is not defined` を解消。[証拠](phase191/evidence/20261014-q1016/README.md)。p173全件100%未達。

Outcome: q1017-i01 partial cleared、p191 whole uncleared。対象WPT18/18 subtest PASS、固定textdecoration全16件5 PASS/11 FAIL、subtest79 PASS/100 FAIL。host build・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1017/README.md)。全件100%未達。

Outcome: q1018-i01 partial cleared、p191 whole uncleared。対象WPT67/67 subtest PASS、固定textdecoration全16件6 PASS/10 FAIL、subtest128 PASS/51 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1018/README.md)。全件100%未達。

Outcome: q1019-i01 partial cleared、p191 whole uncleared。対象4 WPT16/16 subtest・invalid既存2 WPT PASS、固定textdecoration全16件10 PASS/6 FAIL、subtest144 PASS/35 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1019/README.md)。全件100%未達。

Outcome: q1020-i01 partial cleared、p191 whole uncleared。対象2 WPT6/6 subtest・invalid既存WPT PASS、固定textdecoration全16件12 PASS/4 FAIL、subtest150 PASS/29 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1020/README.md)。全件100%未達。

Outcome: q1021-i01 partial cleared、p191 whole uncleared。invalid WPT3/3 subtest PASS、固定textdecoration全16件13 PASS/3 FAIL、subtest153 PASS/26 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1021/README.md)。全件100%未達。

Outcome: q1022-i01 partial cleared、p191 whole uncleared。valid17/17・shorthand5/5 subtest、固定textdecoration全16件15 PASS/1 FAIL、subtest165 PASS/14 FAIL。host build warning0・CSSOM12/12・rule model44/44、Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1022/README.md)。全件100%未達。

Outcome: q1023-i01 / p191 whole cleared。固定textdecoration全16/16 test、179/179 subtest PASS。host build warning0・CSSOM12/12・rule model44/44、Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1023/README.md)。p173全3,805件100%は未達。

Outcome: q1024-i01 partial cleared、p189 whole uncleared。選定100件の実測は9 PASS/68 FAIL/3 harness error/7 no completion/13 no output。4領域の単一HTTP候補0を記録。[証拠](phase189/evidence/20261014-q1024/README.md)。p173全3,805件100%は未達。

Outcome: q1025-i01 partial cleared、p192 whole uncleared。一意id global lookupを実装し、局所fixture・native property65/65・realm30/30・Acid3差0/Acid2差1865の回帰確認。2 WPTの先頭ReferenceErrorを解消したが双方別の未実装でPASS未達。[証拠](phase192/evidence/20261014-q1025/README.md)。p173全件100%未達。

Outcome: q1026-i01 partial cleared、p193 whole uncleared。HTMLDetailsElement.open反映を追加、fixture・host回帰・Acid3差0/Acid2差1865を確認。対象WPTは次のhidden until-found差分でPASS未達。[証拠](phase193/evidence/20261014-q1026/README.md)。p173全件100%未達。

Outcome: q1027-i01 partial cleared、p193 whole uncleared。固定hidden IDL2 WPTが17/17・1/1 subtest PASS、beforematchはfragment navigation未完でno output。host build warning0・native property65/65・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1027/README.md)。p173全件100%未達。

Outcome: q1028-i01 partial cleared、p193 whole uncleared。同一文書hash・fragment target・beforematch revealをfixtureで確認。固定WPTはcompletion付きFAILへ進行し、次の `Element.attributes` 欠落を特定。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1028/README.md)。p173全件100%未達。

Outcome: q1029-i01 partial cleared、p194 whole uncleared。Element.attributesのlive read surfaceで固定beforematch WPT1/1 PASS。別WPTはreveal順序差でFAIL。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase194/evidence/20261014-q1029/README.md)。p173全件100%未達。

Outcome: q1030-i01 partial cleared、p193 whole uncleared。hidden/openのatom同一性を修正し固定中断2 WPT PASS。別WPTはtoggle待ちでno output。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1030/README.md)。p173全件100%未達。

Outcome: q1031-i01 partial cleared、p193 whole uncleared。非同期toggleをcoalesceし固定hidden/details WPT1/1 PASS、先行beforematch PASS維持。host build warning0・native property65/65・realm30/30・Acid3差0/Acid2差1865。[証拠](phase193/evidence/20261014-q1031/README.md)。p173全件100%未達。

Outcome: q1032-i01 partial cleared、p193 whole uncleared。固定details testharness全18件は6 PASS/10 FAIL/2 no_output。25 reftest/3 crashtestはp190 runner待ち。[証拠](phase193/evidence/20261014-q1032/README.md)。コード無変更、p173全件100%未達。

Outcome: q1033-i01 partial cleared、p190 whole uncleared。選定静的reftest10件は既存runnerで4 provisional PASS/6 FAIL、PASS1件はreference-chain無視のfalse pass。[証拠](phase190/evidence/20261014-q1033/README.md)。コード無変更、p173全件100%未達。

Outcome: q1034-i01 partial cleared、p190 whole uncleared。静的match-only reference chainの16辺上限と誤PASS防止を追加。局所graph4/4 PASS、固定10件は3 PASS/7 FAIL/0 errorへ訂正（先のfalse PASS1件がFAIL）。Python compile/diff check PASS。[証拠](phase190/evidence/20261014-q1034/README.md)。browser/WPT/Master無変更、p173全件100%未達。

Outcome: q1035-i01 partial cleared、p193 whole uncleared。UA CSSのfirst summary list-item既定値を追加し、固定details-display WPTは8/8 subtest PASS。host build warning0・CSS rule model44/44・CSSOM12/12、Acid3差0/480000・Acid2差1865/120000不変。[証拠](phase193/evidence/20261014-q1035/README.md)。p173全件100%未達。
