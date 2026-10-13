<!-- awesome-plan project=zedbsd record=ws074-p210 -->

# ws074-p210: CSS Grid WPT process crash

Status: cleared（q1067固定SIGSEGV消失・host/ASan/Acid確認）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1066](../phase189/evidence/20261014-q1066/README.md) の再現SIGSEGV、[p209 q1065](../phase209/evidence/20261014-q1065/README.md) のfont ready出力。

## 目的と受け入れ

固定 `/css/css-grid/grid-items/grid-minimum-size-grid-items-021.html` は q1066 の固定font/localhost条件で144 result callback後に exit -11 となり、同条件で再現した。期待はブラウザが安全に終了しWPT completionを返すこと。根本原因を再現可能な一般条件に縮め、browser内部の最小修正と host/WPT/Acid 回帰でSIGSEGV消失を確認する。WPTのFAILをPASSへ強制しない。p189 whole・p173全3,805件100%の受け入れは独立。

## q1067-i01 有限部分範囲

2026-10-14 04:08 JST、このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（05:38 JSTまで）に適用。上記固定1 IDについてASan/デバッガでexit -11を観測し、ページ構成から最小再現条件を得て一般的なメモリ/レイアウト欠陥だけを修正する。固定WPT completion取得、局所host検査、warning0 build、Acid3画素差0/Acid2非増加を確認。必要なら近縁Grid代表を有限数で測る。WPT/runner/公開ABI/`plan/master.md`変更なし。上限内に安全な修正が成立しなければ症状/stack/試行/次条件を記録してunclearedで止める。適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p210-addition-q1067-20261014: q1066の固定280 ID中1件に同条件再現SIGSEGV。p189 runnerの結果分類は正しくprocess_error、browser一般欠陥を本Phaseで担当する。p189/p173の分母を維持。共有Bug Boardへのticket投影はQ1競合回避のため保留。

## q1067-i01 結果（2026-10-14 04:13 JST）

Event ws074-p210-q1067-clear-20261014: 固定Grid WPTは旧通常起動3/3 SIGSEGV。pre-fix ASanが`vm_heap_mark`の手動管理realm pointerを未登録large cellとして扱うheap-buffer-overflowを特定。[証拠](evidence/20261014-q1067/README.md)。登録済みlarge割当とcell先頭一致を確認してmarkする一般修正後、通常3/3・ASan1/1でexit0/completion取得、WPT test自体はGrid assertionでFAIL。host-heap31/31・realms30/30・native property65/65・parser8/8、plain/ASan warning0 build、Acid3差0/Acid2差1865。q1067-i01/p210 whole cleared。p189旧process_errorは履歴のまま、次の同条件再計測で更新する。p173全3,805件100%未達、共有投影/GitHub publication保留。
