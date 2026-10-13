<!-- awesome-plan project=zedbsd record=queue -->

# q1018 / WS074 text-decoration-line inline CSSOM canonicalization

Status: finished（2026-10-14 00:20〜00:25 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: 2026-10-14 00:07 JST、この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を、[p191のq1018有限部分scope](phase191/phase.md) に適用。
Timebox: 2026-10-14 00:20 JST 開始、最大90分（01:50 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1018-i01 | [ws074-p191](phase191/phase.md) partial | text-decoration-lineのinline CSSStyleDeclaration格納値を既存parserに基づきcanonical化。CSS-wide/他property/不正値維持。局所fixture、build、対象WPT67 subtest、全16件、Acid3/Acid2非増加。WPT/Master/公開ABI変更なし。最大90分 | cleared（partial、p191 whole は uncleared） | q1017値parser/computed出力 |

Dependency graph: p173固定manifest → p189 q1015 baseline → q1016 CSS.supports → q1017値解析 → q1018-i01 → p191 whole判断 → p173最終100%判断。p189 whole/p190は別の必要出力。

Upcoming Work Outlook: p191の残るtextdecoration CSSOM、p189 runnerの環境対応、[p190](phase190/phase.md) reftest/crashtest coverage、p173全件baselineを候補とする。次の Queue も有限scopeを記録する。

Coordination: [q1014](../history/queue-q1014.md) までの履歴を保持。共有 `plan/queue.md` / Master / GitHub publication は別の Q1 セッションとの競合を避け pending projection。WS074 lane の状態はこの file が所有。

Previous outcome: q1013-i01 partial cleared、p189 whole uncleared。選定testharness1件のWPT callbackから12 subtestとcompletionを取得し、4 PASS/8 FAIL、harness OK、runnerの結果はFAIL。status分類の unit5/5、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1013/README.md)。残り1,782 testharnessと p173全件baseline、100%は未達。

Outcome: q1014-i01 partial cleared、p189 whole uncleared。19領域から条件を満たす15 IDを選定・実行、12 test FAIL・1 harness ERROR・1 completion無し・1 stdout無し、PASS0。4領域は未実行理由を保存。選定 unit2/2、py_compile、diff check PASS。[証拠](phase189/evidence/20261014-q1014/README.md)。q1013 と合わせユニーク16/1,783 testharnessを試行。ブラウザ/WPT/Master無変更、Acid3再計測省略。p173全件baseline・100%未達、共有Master/Queue/GitHub publication pending。

Outcome: q1015-i01 partial cleared、p189 whole uncleared。textdecorationの全16 IDを実測、4 PASS/12 FAIL、subtest47 PASS/132 FAIL。全16 completion取得。`CSS` global欠落とtext-decoration CSSOM不足を共通原因候補として記録。[証拠](phase189/evidence/20261014-q1015/README.md)。browser/WPT/Master無変更、p173全件baseline・100%未達。

Outcome: q1016-i01 partial cleared、p191 whole uncleared。`CSS.supports` を宣言parserへ接続。host fixture、build warning0、CSSOM12/12・rule model44/44、固定16件、Acid3差0を検証。textdecoration4 PASS/12 FAILと数不変だが `CSS is not defined` を解消。[証拠](phase191/evidence/20261014-q1016/README.md)。p173全件100%未達。

Outcome: q1017-i01 partial cleared、p191 whole uncleared。対象WPT18/18 subtest PASS、固定textdecoration全16件5 PASS/11 FAIL、subtest79 PASS/100 FAIL。host build・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1017/README.md)。全件100%未達。

Outcome: q1018-i01 partial cleared、p191 whole uncleared。対象WPT67/67 subtest PASS、固定textdecoration全16件6 PASS/10 FAIL、subtest128 PASS/51 FAIL。host build warning0・局所fixture・Acid3差0/Acid2差1865の回帰確認。[証拠](phase191/evidence/20261014-q1018/README.md)。全件100%未達。
