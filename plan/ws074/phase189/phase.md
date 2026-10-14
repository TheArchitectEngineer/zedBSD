<!-- awesome-plan project=zedbsd record=ws074-p189 -->

# ws074-p189: Interop 2025 testharness 結果取得

## q1189-i01 結果（2026-10-14 13:56 JST）

Event ws074-p189-q1189-partial-clear-20261014: [証拠](evidence/20261014-q1189/README.md)。固定Grid502同条件再測定は189→192 whole PASS、313→310 FAIL、16 callback改善、後退0、未出力0。同一ID/order/hash/checkpoint/rawを確認。改善はnon-static007/008とmargin/border/padding005。残る上位はstretch12件、margin/border/padding11件。q1189有限部分範囲cleared、p189/p173全3,805件100%未達、共有投影/GitHub publication保留。

## q1189-i01 有限部分範囲（2026-10-14 13:54 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（15:24 JSTまで）に適用。q1077固定Grid502同一ID・WPT commit・font・5秒settle/20秒process条件を4 shardで再測定し、q1177基準189 PASS/313 FAILとwhole/subtest差分、後退・no_outputを検証する。全raw callback/checkpointを保存し、残FAILの次候補を分類。読取と証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1177-i01 結果（2026-10-14 13:23 JST）

Event ws074-p189-q1177-partial-clear-20261014: [証拠](evidence/20261014-q1177/README.md)。固定Grid502同条件再測定は183→189 whole PASS、319→313 FAIL、27 callback改善、後退0、未出力0。同一ID/order/hash/checkpoint/rawを確認。改善はnon-static-positioned001–006の6件、残る上位はmargin/border/padding系列とstretch系列各12件。q1177有限部分範囲cleared、p189/p173全3,805件100%未達、共有投影/GitHub publication保留。

## q1177-i01 有限部分範囲（2026-10-14 13:21 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（14:51 JSTまで）に適用。q1077固定Grid502同一ID・WPT commit・font・settle/process条件を4 shardで再測定し、q1172基準183 PASS/319 FAILとwhole/subtest差分、後退・no_outputを検証する。全raw callback/checkpointを保存し、残FAILの次候補を分類。読取と証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1172-i01 有限部分範囲（2026-10-14 12:57 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（14:27 JSTまで）に適用。q1160と同一固定Grid testharness502 ID・WPT/manifest/フォント/localhost HTTP/settle5s/timeout20sで4×最大128 shardを再測定し、q1160 baseline158 PASS/344 FAILとのID・whole/subtest遷移、未出力・後退を照合。raw callback gzip/checkpointを保存。読取・証拠のみ、browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。未完なら未実行を明示してuncleared。p189/p173 whole基準外。[Guardrail](../../guardrail.md)とrunner近傍規則適用。

## q1172-i01 結果（2026-10-14 13:03 JST）

Event ws074-p189-q1172-partial-clear-20261014: [証拠](evidence/20261014-q1172/README.md)。固定Grid502同条件4 shard exit0、ID順/全callback名とraw gzip/checkpoint502照合。q1160の158 PASS/344 FAIL→183 PASS/319 FAIL、25 whole・50 subtest改善、旧PASS/subtest後退0。増分はpositioned列/行軸001–011計22件とmargin/border/padding001–003計3件。q1172部分cleared、p189全1,783件とp173全3,805件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1160-i01 有限部分範囲（2026-10-14 12:20 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:50 JSTまで）に適用。q1154固定Grid502と同じID/WPT/metadata/runner/font/5秒settle/20秒processで4 bounded shardを全再測定。ID/hash/checkpoint/gzip rawを照合し、whole/subtest遷移・旧PASS後退・残FAILを分類。読取のみ、browser/runner/WPT/ABI/`plan/master.md`/Acid変更なし。p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1160-i01 結果（2026-10-14 12:22 JST）

Event ws074-p189-q1160-partial-clear-20261014: [固定Grid502全再測定](evidence/20261014-q1160/README.md)でq1154の142→158 whole PASS、360→344 FAIL。改善16件は縦右Grid stretch001–016、subtest16改善・旧PASS/subtest後退0。502 ID/hash/checkpoint/gzip raw一致。q1160診断部分cleared、p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1155-i01 有限部分範囲（2026-10-14 12:07 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:37 JSTまで）に適用。q1154固定Grid502の残360 whole FAILをWPTパス、subtest失敗数、初回失敗メッセージ/期待・実測で機械分類し、代表原本を確認して次の有限実装候補と検証境界を記録する。読取のみ、browser/runner/WPT/ABI/`plan/master.md`/Acid変更なし。p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1155-i01 結果（2026-10-14 12:07 JST）

Event ws074-p189-q1155-partial-clear-20261014: [残Grid360全分類](evidence/20261014-q1155/README.md)でalignment176が最大、初回width症状155。vertical-rl stretch16件全FAILは親Gridと子inline双方の縦右向け未対応が候補。次は非Grid文字geometry/paintをp220で診断し、固定/auto Gridは後続有限範囲。q1155診断部分cleared、p189/p173全件100%未達。読取のみ、共有投影/GitHub publication保留。

## q1154-i01 有限部分範囲（2026-10-14 12:03 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:33 JSTまで）に適用。q1151の固定Grid502と同じID/WPT/metadata/runner/font/5秒settle/20秒processで4 bounded shardを全再測定。ID/hash/checkpoint/gzip rawを照合し、whole/subtest遷移・旧PASS後退・残FAILを分類。読取のみ、browser/runner/WPT/ABI/`plan/master.md`/Acid変更なし。p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1154-i01 結果（2026-10-14 12:06 JST）

Event ws074-p189-q1154-partial-clear-20261014: [固定Grid502全再測定](evidence/20261014-q1154/README.md)でq1151の136→142 whole PASS、366→360 FAIL。改善6件は縦Grid stretch010/011/012/014/015/016、subtest6改善・旧PASS/subtest後退0。502 ID/hash/checkpoint/gzip raw一致。q1154診断部分cleared、p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1151-i01 有限部分範囲（2026-10-14 11:49 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:19 JSTまで）に適用。q1147と同じ固定Grid適格testharness502 IDを、q1150 implicit auto track実装後に同一WPT commit/metadata・localhost HTTP・固定font3種・5秒settle/20秒processで最大128件×4 shard再測定する。ID/hash/checkpoint/gzip rawとq1147 whole/subtest遷移・旧PASS後退・縦Grid009/013を照合する。browser/WPT/runner/公開ABI/`plan/master.md`/Acid変更なし。90分内未完なら完了済み/未実行を分けてuncleared。p189 whole1,783件・p173全3,805件100%は部分基準外。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。

## q1151-i01 結果（2026-10-14 11:52 JST）

Event ws074-p189-q1151-partial-clear-20261014: [証拠](evidence/20261014-q1151/README.md)。固定Grid502件はq1147の134→136 whole PASS、368→366 FAIL、無出力0。2 whole/subtest FAIL→PASS（vertical-lr stretch009/013）、旧PASS/subtest後退0。ID/hash/checkpoint502行/gzip raw502件照合。q1151-i01測定部分cleared、p189 wholeとp173全3,805件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1147-i01 有限部分範囲（2026-10-14 11:21 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:51 JSTまで）に適用。q1145と同じ固定Grid適格testharness502 IDを、q1146横書き子の縦Grid軸実装後に同一WPT commit/metadata、localhost HTTP、固定font3種、5秒settle/20秒processで最大128件ずつ4 shard再測定する。ID/hash/checkpoint/gzip rawとq1145 whole/subtest遷移・旧PASS後退・縦Grid familyを照合する。browser/WPT/runner/公開ABI/`plan/master.md`変更なし。90分内未完なら完了済み/未実行を分けてuncleared。p189 whole1,783件・p173全3,805件100%は部分基準外。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。

## q1147-i01 結果（2026-10-14 11:25 JST）

Event ws074-p189-q1147-partial-clear-20261014: [証拠](evidence/20261014-q1147/README.md)。固定502 IDはq1145の130→134 whole PASS、372→368 FAIL、無出力0。4 whole/subtest FAIL→PASS、旧PASS/subtest後退0。増加はvertical-lr stretch005–008。ID/hash/checkpoint502行/gzip raw502件照合。q1147-i01測定部分cleared、p189 wholeとp173全3,805件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1145-i01 有限部分範囲（2026-10-14 11:12 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:42 JSTまで）に適用。q1143と同じ固定Grid適格testharness502 IDを、q1144縦inline geometry後に同一WPT commit/metadata、localhost HTTP、固定font3種、5秒settle/20秒processで最大128件ずつ4 shard再測定する。ID/hash/checkpoint/gzip rawとq1143 whole/subtest遷移・旧PASS後退・縦Grid familyを照合する。browser/WPT/runner/公開ABI/`plan/master.md`変更なし。90分内未完なら完了済み/未実行を分けてuncleared。p189 whole1,783件・p173全3,805件100%は部分基準外。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。

## q1145-i01 結果（2026-10-14 11:16 JST）

Event ws074-p189-q1145-partial-clear-20261014: [証拠](evidence/20261014-q1145/README.md)。固定502 IDはq1143の126→130 whole PASS、376→372 FAIL、無出力0。4 whole/subtest FAIL→PASS、旧PASS/subtest後退0。増加はvertical-lr stretch001–004。残005–008はhorizontal child、009–016は明示Grid寸法と可変trackの限定経路外。ID/hash/checkpoint502行/gzip raw502件照合。q1145-i01測定部分cleared、p189 wholeとp173全3,805件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1143-i01 有限部分範囲（2026-10-14 10:59 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:29 JSTまで）に適用。q1140と同じ固定Grid適格testharness502 IDを、q1142縦Grid軸コード後に同一WPT commit/metadata、localhost HTTP、固定font3種、5秒settle/20秒processで最大128件ずつ4 shard再測定する。ID/hash/checkpoint/gzip rawとq1140 whole/subtest遷移・旧PASS後退を照合する。browser/WPT/runner/公開ABI/`plan/master.md`変更なし。90分内未完なら完了済み/未実行を分けてuncleared。p189 whole1,783件・p173全3,805件100%は部分基準外。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。

## q1143-i01 結果（2026-10-14 11:02 JST）

Event ws074-p189-q1143-partial-clear-20261014: [証拠](evidence/20261014-q1143/README.md)。固定502 IDはq1140と同じ126 whole PASS/376 FAIL、5,163 PASS/5,842 FAIL subtest、無出力0。whole/subtest状態変化0、旧PASS後退0。ID/hash/checkpoint502行/gzip raw502件を照合。vertical-lr stretch001の初回文字幅FAILは期待10/実100→60へ変化したが未PASS。q1143-i01測定部分cleared、p189 wholeとp173全3,805件100%は未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1140-i01 有限部分範囲（2026-10-14 10:41 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:11 JSTまで）に適用。q1134と同じ固定Grid適格testharness502 IDを同一WPT commit、localhost HTTP、固定font3種、5秒settle/20秒processで最大128件ずつ4 shard再測定。全ID/hash/checkpoint/gzip raw、q1134 whole/subtest遷移・旧PASS後退を照合し、残る共通失敗群を分類する。browser/WPT/runner/公開ABI/`plan/master.md`変更なし。90分内に完走しない場合は完了済みと未実行を分けてunclearedで止める。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。p189 whole1,783件とp173全3,805件100%はこの部分受け入れに含めない。

## q1140-i01 結果（2026-10-14 10:47 JST）

Event ws074-p189-q1140-outcome-20261014: [証拠](evidence/20261014-q1140/README.md)。固定Grid502件は124→126 whole PASS、378→376 FAIL、no_output0。2 FAIL→PASS、旧PASS後退0。subtestは5,118→5,163 PASS、5,887→5,842 FAIL（45改善/0後退）。全502 ID/hash/checkpoint/gzip raw照合、4 shard正常終了。q1140-i01測定部分cleared、p189 wholeとp173全3,805件100%は未達。共有投影/GitHub publication保留。

## q1134-i01 有限部分範囲（2026-10-14 10:18 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（11:48 JSTまで）に適用。q1105と同じ固定Grid適格testharness502 IDを同一WPT commit、localhost HTTP、固定font3種、5秒settle/20秒processで最大128件ずつ4 shard再測定。全ID/hash/checkpoint/gzip raw、q1105 whole/subtest遷移・旧PASS後退を照合し、残る共通失敗群を分類する。browser/WPT/runner/公開ABI/`plan/master.md`変更なし。90分内に完走しない場合は完了済みと未実行を分けてunclearedで止める。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。p189 whole1,783件とp173全3,805件100%はこの部分受け入れに含めない。

## q1134-i01 結果（2026-10-14 10:24 JST）

Event ws074-p189-q1134-outcome-20261014: [証拠](evidence/20261014-q1134/README.md)。固定Grid502件は110→124 whole PASS、392→378 FAIL、no_output0。16 FAIL→PASSと2旧PASS→FAIL。subtestは5,225→5,118 PASS、5,780→5,887 FAIL（398改善/505後退）。後退505中416件はGrid行track animation2ファイル。empty-gridはmin-content幅0期待が200となる6件。全502 ID/hash/checkpoint/gzip rawを照合、4 shard正常終了。q1134-i01測定部分cleared、p189 wholeとp173全3,805件100%は未達。旧PASS後退の実装修正は次の有限Queueへ渡す。共有投影/GitHub publication保留。

Status: uncleared（2026-10-14 q1013/q1014/q1015 partial cleared、whole 1,783件は未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173 q1012 manifest](../phase173/evidence/20261013-q1012/manifest.json) の固定選定出力。p173 whole clearance は要しない。

## 目的と受け入れ

Interop 2025 固定 manifest の testharness 1,783件について、WPT のテスト本体と期待を変更せずに実行し、test/subtest の PASS・FAIL・ERROR・TIMEOUT・未実行を区別して保存できる runner を作る。HTTP/HTTPS、origin、worker、variant などの実行環境差は明示して段階的に対応し、未実行を PASS に算入しない。最終的に全1,783件の結果と、WPT実行環境の未対応が0であることを確かめた時に whole clearance を判断する。ブラウザ実装の不足による実際の FAIL は p173 の後続互換性 Phase に渡し、本Phaseの runner 完成と混同しない。

## 最初の有限 attempt 案（未承認）

最大90分。既存 `browser --run` と WPT の testharness callback を調べ、単一 HTTP origin の選定済み testharness HTML 1件で subtest と completion を機械可読に取得する。runner の最小実装、成功/失敗/timeout/無出力の区別、固定 manifest に載るIDだけを受け付ける検査と再現可能な fixture を含む。testharness.js 自体の改変、テスト固有の PASS 強制、browser engine/公開ABIの変更は含めない。もし WPT の JS harness が現行 engine で構文/機能エラーになれば、検証結果と最小原因を記録して attempt を uncleared とし、browser の別Phaseを設計する。p173 q1012 で `--run` に選定ページを1件渡した際は exit0・console出力0で、結果判定はできなかった。

適用: [Guardrail](../../guardrail.md)、runner の近傍規則、browser sourceに進む場合は [C全文](../../coding-style.md) と [browser component全文](../../standards/browser-component.md)。影響候補は `plan/ws074/tests/` と `plan/ws074/phase189/`、ignored build。新しい code と実行は exact-scope Queue 承認後のみ。`master.md` は変更しない。

Event ws074-p173-q1012-split-20261013: [p173](../phase173/phase.md) の3,805 ID のうち1,783件が testharness で、現行 static reftest runner は結果取得できないため設計。p173 の目標・分母は縮小せず、本Phaseの出力を p173 全件 baseline の前提にする。GitHub publication保留。

Queue: [q1013-i01](../queue.md) partial。2026-10-13 23:59 JST にユーザーが上記の最初の有限 attempt 範囲を承認。最大90分。browser本体変更なし。

## q1013-i01 結果（2026-10-14 00:03 JST）

**partial scope cleared、p189 whole は uncleared。** 公式manifestにある `/css/css-anchor-position/at-position-try-parse.html` のWPT `testharnessreport.js` HTTP応答に、WPTが公開する `add_result_callback` / `add_completion_callback` の記録処理を追加して実行した。WPT test本体・check-outに変更なし。結果は[証拠](evidence/20261014-q1013/README.md)に保存。process exit0、harness completion OK、12 subtestを全件取得し、4 PASS/8 FAIL。runner全体は正しく FAIL/exit1 とする。最初の CSS rule は「The input is not one supported CSS rule.」であり、browser実装の差として別の互換性改善が必要。browser source/APIは変更していない。

runner の結果分類は PASS、FAIL、WPT timeout、process timeout、無出力、completion欠落、harness error、process errorを区別し、未実行をPASSにしない。Python unit5/5、py_compile、`git diff --check` PASS。browser source無変更のため Acid3の再計測はしない。実行環境は host、単一localhost HTTP origin、固定WPT commit、5秒virtual settle、20秒process timeout。HTTPS/worker/multi-origin/variantや残り1,782 testharness は未測定。p189 wholeと p173 whole/全件100%は未達。

Event ws074-p189-q1013-outcome-20261014: q1013-i01 partial cleared、p189 whole uncleared。実際の test FAIL とrunner result取得を区別した。残る1,782件と環境対応の有限 Queue 再開が必要。GitHub publication保留。

Queue: [q1014-i01](../queue.md) partial。2026-10-14 00:04 JST ユーザーが [最大90分の範囲](next-queue-proposal.md)を承認。最大19件、browser/WPT/Master変更なし。

## q1014-i01 結果（2026-10-14 00:07 JST）

**partial scope cleared、p189 whole は uncleared。** 公式固定manifestの19 focus areaから単一HTTP・実ファイルHTML・driver不要の範囲で各最大1件を決定的に選定し、15 IDを実行。重複なし。Storage Access API、URLPattern、WebAssembly、WebRTC は条件を満たす候補がなく未実行と明記。[証拠](evidence/20261014-q1014/README.md)に全選定理由・ID・raw reportを保存。結果は **0 PASS、12 test FAIL、1 harness ERROR、1 completion無し、1 stdout無し**。無出力や未実行はPASSとしていない。q1013のIDを加えるとこのlaneで試したユニークIDは16/1,783。正式全件率とはしない。

最初の `align-self: anchor-center` 不受理、`main is not defined`、`a1 is not defined` 等の失敗を観測。現runnerのHTTP/自動操作/worker環境との切り分けが未完了なので全てをbrowser欠陥と断定しない。batch選定の重複・除外の unit2/2、py_compile、diff check PASS。browser/WPT/Masterの変更なし、Acid3は再計測しない。残る1,767 testharness、4領域の環境、p190の2,022件、p173全件baseline/100%は未達。

Event ws074-p189-q1014-outcome-20261014: q1014-i01 partial cleared、p189 whole uncleared。上記の15 ID結果と4領域の未実行理由を保存。次は実行環境整備と大量shard、または失敗群の最小原因調査を有限Queueで選定する。GitHub publication保留。

## q1015-i01 結果（2026-10-14 00:11 JST）

ユーザーの Interop 2025 全件PASSまでの有限 Queue 事前承認に基づき、固定manifestのtextdecoration領域全16 testharness IDを測定。**partial scope cleared、p189 whole uncleared。** 4 PASS/12 FAIL、subtest47 PASS/132 FAIL、全16 completion取得。infra failure0。[証拠](evidence/20261014-q1015/README.md)。computed系で `CSS` global がなく、CSSOM の text-decoration longhand・shorthand 不足が広く見える。browser/WPT/Master source変更なし。次のbrowser修正は新しい有限Queueにする。

Event ws074-p189-q1015-outcome-20261014: 16 IDの正式結果と失敗群を保存。textdecoration領域も全件100%未達、p189の他IDとp173全件baselineが残る。GitHub publication保留。

## q1024-i01 の有限部分範囲

2026-10-14 00:46 JST、ユーザーの事前承認を適用、最大90分。固定manifestの19 focusから、q1014と同じ単一HTTP・物理HTML・driver/worker不要の候補を各最大8件、全体最大128 unique ID決定的に選定し、WPT callback runnerで測る。重複、候補除外理由、PASS/FAIL/harness ERROR/timeout/no-outputなどを機械可読に保存し、代表的な失敗の原因候補を分類する。ブラウザ、WPT本体、公開ABI、`master.md`は変更しない。selectionの局所検査・Python compile・固定入力照合・runner結果の保存を確認する。p189 whole 1,783件と全件PASSはこの部分基準に含めない。

## q1024-i01 結果（2026-10-14 00:48 JST）

**partial scope cleared、p189 whole uncleared。** 固定manifestから各focus最大8・全体100 unique IDを選び実行。[証拠](evidence/20261014-q1024/README.md)。9 PASS、68 FAIL、3 harness error、7 completion無し、13 stdout無し。4領域は現条件で候補0。anchor/backdrop CSS不足、named element global欠落、module script未実行、async event待ちなどを原因候補として分離。結果JSONに選定・除外理由・生callbackを保存。Python compile、diff check PASS。browser/WPT/Master無変更、Acid再計測省略。p189 whole1,783件とp173全3,805件100%は未達、GitHub publication保留。

Event ws074-p189-q1024-outcome-20261014: q1024-i01 partial cleared、p189 whole uncleared。100 unique IDの結果と環境別の残件を保存。次はmodule等の環境分類を正確化し、browser共通原因を有限Queueで処理する。

## q1063-i01 の有限部分範囲（2026-10-14 03:38 JST）

このchatのユーザーによる「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を最大90分 Queue（03:38–05:08 JST）に適用。固定manifestの1,783 testharnessから、既存の単一localhost HTTP・物理HTML・driver/worker等不要という候補判定を保ち、全候補をID順に列挙してoffset/limitで決定的な有限shardを選べるrunnerを追加する。各件を機械可読結果と逐次checkpointに保存し、最初の最大128 unique IDを現行host browserで実測する。manifest/WPT commit・選定ID・結果分類・checkpointの整合を検証し、対象外/未実行をPASSにしない。既存q1024と同じ理由分類を維持する。browser/WPT/公開ABI/`plan/master.md`は変更しない。runが90分に収まらなければcheckpointと未実行件数を残してunclearedとする。適用規則は[Guardrail](../../guardrail.md)、Python runner近傍のcopyright/Zlib header・既存命名・単体検証。p189 whole1,783件とp173全3,805件100%はこの部分範囲の受け入れに含めない。

## q1063-i01 結果（2026-10-14）

Event ws074-p189-q1063-outcome-20261014: 全1,783 testharness IDから現行単一HTTP条件で1,455候補を決定的に列挙できるrunnerを追加し、offset0の128 IDを実測。[証拠](evidence/20261014-q1063/README.md)。18 PASS/93 FAIL/5 harness error/8 completion無し/4 stdout無し。checkpoint128行・raw report・選定ID一致、局所選定2/2、py_compile/diff check PASS。q1063-i01 partial cleared、p189 whole uncleared。残り1,327候補と環境未対応328 ID、110非PASS、p173全3,805件100%が残る。browser/WPT/Master変更なし、共有投影/GitHub publication保留。

## q1064-i01 の有限部分範囲（2026-10-14 03:39 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分 Queue（03:39–05:09 JST）に適用。q1063の現行単一HTTP物理HTML候補1,455 IDの残るoffset128〜末尾1,327 IDを、最大128件ごとの独立shardで固定WPT/manifest/host条件のまま全測定する。raw callback結果を可逆gzipで保存するrunnerの最小変更を含める。各shardのfsync checkpoint、選定ID/件数、全shard合算の重複・欠落・outcome/環境分類を照合し、途中中断なら完了済みと未実行を区別してunclearedで止める。browser/WPT/公開ABI/`plan/master.md`は変更せず、環境未対応328 IDはPASSに算入しない。上限を超えない。適用は[Guardrail](../../guardrail.md)、Python runner近傍規則、py_compile・gzip roundtrip・結果照合。p189 whole全1,783件とp173全3,805件100%はこの部分範囲の受け入れに含めない。

## q1064-i01 結果（2026-10-14 03:53 JST）

Event ws074-p189-q1064-outcome-20261014: q1063の単一HTTP候補1,455 IDの残り1,327を最大128件×11 shardで全測定。[証拠](evidence/20261014-q1064/README.md)。q1063と合算160 PASS/910 FAIL/299 completion無し/73 stdout無し/13 harness error、環境未対応328 IDは未実行。全1,455 ID一意・選定hash、11 checkpoint/report、gzip raw1,327件のID/outcome一致。runnerの可逆gzip保存を追加、py_compile/diff check PASS。q1064-i01 partial cleared、p189 whole uncleared。browser/WPT/Master変更なし、p173全3,805件100%未達、共有投影/GitHub publication保留。

## q1066-i01 有限部分範囲（2026-10-14 04:03 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（05:33 JSTまで）に適用。q1065の`Document.fonts.ready`をWPT testharness runnerの通常起動で検証できるよう、既存runnerに明示的な固定font directory optionを追加し、存在確認とbrowser CLI引数へ接続する。固定manifest/q1064で`document.fonts.ready`欠落の同一TypeErrorだったCSS Grid `no_completion`280 unique IDを、同じWPT/host/localhost/5秒条件＋明示font dirで全再測定し、旧→新の全status・completion/subtestとID/checkpointを照合する。テスト本体/WPT/browser engine/公開ABI/`plan/master.md`は変更しない。PASSに変わらない結果は正直に分類し、90分上限時は完了済み/未実行を保存してunclearedで止める。Python compile・runner option局所検査・結果照合・diff checkで検証する。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。p189 wholeとp173全件100%はこの部分の基準に含めない。

## q1066-i01 結果（2026-10-14 04:07 JST）

Event ws074-p189-q1066-outcome-20261014: runnerに明示固定font directoryを追加、旧`document.fonts.ready`停止のCSS Grid280 IDを全再測定。[証拠](evidence/20261014-q1066/README.md)。19 PASS/260 FAIL/1 process error(-11 SIGSEGV)、279 completion、subtest313/2640 PASS。旧全280 no_completionは0、残るSIGSEGV1は同条件1回再現。ID/checkpoint280/raw gzip280一致、runner unit6/6・py_compile/diff check PASS。q1066-i01 partial cleared、p189 whole uncleared。q1063/q1064との混合条件暫定179 PASS/1170 FAIL/1 process error/19 no_completion/73 no_output/13 harness error、環境未対応328。browser/WPT/Master変更なし、p173全3,805件100%未達、共有投影/GitHub publication保留。

Event ws074-p189-q1067-p210-followup-20261014: q1066旧process_error1 IDは[p210](../phase210/phase.md)のGC一般修正後、同条件で通常3/3・ASan1/1 completion付きFAILへ進んだ。[証拠](../phase210/evidence/20261014-q1067/README.md)。q1066当時の結果は書き換えず、p189全件の最新集計は次Queueで同条件再測定する。p189 whole/p173全件100%は未達。

## q1068-i01 有限部分範囲（2026-10-14 04:14 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（05:44 JSTまで）に適用。q1066の明示固定font optionをcensus shard runnerへ接続し、q1063/q1064と同じ単一HTTP物理HTML適格1,455 unique testharness IDを、p209 font ready・p210 GC修正後の現行host browserで全件同条件（固定WPT/manifest・localhost・5秒settle・20秒process・固定font3種）測定する。各最大128件shardのfsync checkpoint、gzip raw、ID集合/hash、全遷移・focus・non-PASS分類を照合する。runnerのfont option以外のbrowser/WPT/公開ABI/`plan/master.md`変更なし。90分内に終わらなければ完了済み/未実行を保存してunclearedで止める。Python compile・font option局所検査・結果照合・diff checkを行う。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。p189 whole全1,783件とp173全3,805件100%はこの部分の基準に含めない。

## q1068-i01 結果（2026-10-14 04:40 JST）

Event ws074-p189-q1068-outcome-20261014: 単一HTTP適格testharness1,455 IDを固定font/修正後host browserで同条件全再測定。[証拠](evidence/20261014-q1068/README.md)。223 PASS/1135 FAIL/10 completion無し/73 stdout無し/14 harness error。旧q1063/q1064比63 FAIL→PASS・19 no_completion→PASS、19旧PASS→FAIL候補（anchor14・sideways scroll4・Flex radio1）。q1066同font280件は19 PASS/260 FAIL維持、旧SIGSEGV1はp210修正でcompletion付きFAIL。12 shard正常終了、ID/hash/checkpoint/gzip raw1455一致、runner unit6/6・py_compile/diff check PASS。q1068-i01 partial cleared、p189 whole uncleared。環境未対応328と非PASS1232、p173全3805件100%が残る。browser/WPT/Master無変更、共有投影/GitHub publication保留。

## q1069-i01 有限部分範囲（2026-10-14 04:41 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（06:11 JSTまで）に適用。q1068で旧PASS→FAILになった固定19 unique testharness ID（anchor14・sideways scrollIntoView4・Flex radio1）を、現行同一host browser/WPT/manifest/localhost条件でfont指定なしの旧環境を再実行し、q1064旧・q1068固定font両方のraw callback/subtest・コマンド・geometry値と照合する。真のbrowser修正後退と旧runnerのfont未指定による見かけのPASSを分け、未確定は未確定とする。browser/WPT/runner/公開ABI/`plan/master.md`変更なし、原因修正は含めない。ID/checkpoint/raw/分類一致とdiff checkで検証し、上限時は部分証拠を残す。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。p189 whole/p173全件100%はこの部分基準に含めない。

## q1069-i01 結果（2026-10-14 04:42 JST）

Event ws074-p189-q1069-outcome-20261014: q1068旧PASS→FAIL19 IDを現行同一browserのfont未指定条件で再実行し全19 completion付きPASS。[証拠](evidence/20261014-q1069/README.md)。局所100×20 boxはfont未指定時0×0、固定font時100×20で、旧PASSはgeometry欠落の見かけの結果と判定。anchor14・sideways scroll4・Flex radio1の固定font FAILは実際の未達。ID/checkpoint/raw全19一致、diff check PASS。q1069-i01 partial cleared、p189 whole uncleared。q1068固定font223 PASS/1232非PASSを最新baselineとして維持、環境未対応328、p173全3805件100%未達。browser/WPT/runner/Master無変更、共有投影/GitHub publication保留。

## q1072-i01 有限部分範囲（2026-10-14 05:11 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（06:41 JSTまで）に適用。固定manifestの `css/css-grid/` testharnessを抽出し、既存単一HTTP物理HTML適格判定と同じ WPT/host/localhost/5秒settle/20秒process/固定font条件で全適格IDを最大128件ずつ決定的shard測定する。q1068の同一IDとのoutcome/subtest遷移、旧PASS回帰、ID集合/checkpoint/gzip rawを照合し、Grid固有の次の一般欠陥群を分類する。環境未対応・未実行・no_outputをPASSに数えない。runner/browser/WPT本体/公開ABI/`plan/master.md`変更なし。90分以内に完走しない場合、完了済みと未実行を別記してunclearedで止める。適用: [Guardrail](../../guardrail.md)とrunner近傍規則。p189 whole全1,783件とp173全3,805件100%はこの部分受け入れに含めない。

## q1072-i01 結果（2026-10-14 05:22 JST）

Event ws074-p189-q1072-outcome-20261014: 固定Grid testharness適格502 IDを最大128件×4 shardで同条件全件再測定。[証拠](evidence/20261014-q1072/README.md)。旧41 PASS/429 FAIL/32 no_output→新43 PASS/427 FAIL/32 no_output。2 ID whole FAIL→PASS、旧PASS後退0。subtest PASS1777→1809（+32）。no_output32件は全てmodule scriptを含む2系列。ID集合/hash/checkpoint/gzip raw全502一致、4 shard exit0、diff check PASS。q1072-i01 partial cleared、p189 whole uncleared。残るGrid427 FAIL/32 no_outputとGrid以外の適格対象、環境未対応328、p173全3,805件100%未達。browser/runner/WPT/Master無変更、共有投影/GitHub publication保留。

## q1074-i01 有限部分範囲（2026-10-14 05:38 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（07:08 JSTまで）に適用。[p213 q1073](../phase213/evidence/20261014-q1073/README.md)のGrid auto幅修正後、固定manifestの `css/css-grid/` testharness適格502 IDをq1072と同じWPT/host/localhost/固定font/5秒settle/20秒processで最大128件ずつ決定的shard測定する。q1072とのID/outcome/subtest/raw・旧PASS回帰を照合し、残る共通失敗群を分類する。runner/browser/WPT/ABI/`plan/master.md`変更なし。90分以内に完走しない場合、完了済みと未実行を分けてunclearedで止める。[Guardrail](../../guardrail.md)・既存runner規則を適用。p189 whole全1,783件とp173全3,805件100%はこの部分の受け入れに含めない。

## q1074-i01 結果（2026-10-14 05:44 JST）

Event ws074-p189-q1074-outcome-20261014: 固定Grid testharness適格502 IDを同条件で再測定。[証拠](evidence/20261014-q1074/README.md)。旧43 PASS/427 FAIL/32 no_output→新57 PASS/413 FAIL/32 no_output、14 whole FAIL→PASS・旧PASS後退0、subtest PASS1809→1851（+42）。4 shard exit0、全502 ID/hash/checkpoint/gzip raw照合済み。残る32 no_outputはmodule script2系列、413 FAIL中alignment配下220。q1074-i01 partial cleared、p189 wholeとp173全3,805件100%は未達。runner/browser/WPT/ABI/Master変更なし、共有投影/GitHub publication保留。

## q1077-i01 有限部分範囲（2026-10-14 06:23 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（07:53 JSTまで）に適用。[p214 q1075](../phase214/evidence/20261014-q1075/README.md) と [p215 q1076](../phase215/evidence/20261014-q1076/README.md) のGrid幅/normal/replaced修正後、固定 `css/css-grid/` 適格testharness502 IDをq1074と同じWPT/host/localhost/固定font/5秒settle/20秒processで最大128件ずつ決定的shard測定する。q1074とのID/outcome/subtest/raw・旧PASS回帰を照合し、残る共通失敗群を分類する。runner/browser/WPT/ABI/`plan/master.md`変更なし。90分内に完走しない場合、完了済みと未実行を分けてunclearedで止める。[Guardrail](../../guardrail.md)と既存runner規則を適用。p189 whole全1,783件とp173全3,805件100%はこの部分の受け入れに含めない。

## q1077-i01 結果（2026-10-14 06:28 JST）

Event ws074-p189-q1077-outcome-20261014: 固定Grid testharness適格502 IDを同条件で再測定。[証拠](evidence/20261014-q1077/README.md)。旧57 PASS/413 FAIL/32 no_output→新71 PASS/399 FAIL/32 no_output、14 whole FAIL→PASS・旧PASS後退0、subtest PASS1851→1877（+26）。4 shard exit0、全502 ID/hash/checkpoint/gzip raw照合済み。残る32 no_outputはmodule script2系列、399 FAIL中alignment配下206。q1077-i01 partial cleared、p189 wholeとp173全3,805件100%は未達。runner/browser/WPT/ABI/Master変更なし、共有投影/GitHub publication保留。

## q1078-i01 有限部分範囲と結果（2026-10-14 06:30 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（08:00 JSTまで）に適用。固定Grid no_output32件のmodule source/import形、HTML loader・JS parser/compilerの未対応点、一般的な次の実装境界を読み取り、必要なら既存host/browserでprobeする。製品code/runner/WPT/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)と既存runner規則を適用。結果: [証拠](evidence/20261014-q1078/README.md)に32件全ての同形named import、module AST解析成功、HTMLのmodule skipとcompilerのmodule未対応を記録。[p216](../phase216/phase.md)を追加。q1078-i01 partial cleared、p189 whole/p173全件100%未達。共有投影/GitHub publication保留。

Follow-up ws074-p189-p216-q1086-grid32-20261014: [p216 q1086](../phase216/evidence/20261014-q1086/README.md)で旧q1077 no_output32 IDを同一固定WPT/manifest、localhost、固定font3種、5秒settle/20秒processで再測定し、全32 PASS・各100 subtest（計3,200）を確認。旧no_output ID集合との一致を検証。他のGrid470 IDはこのfollow-upで再測定していないため、p189/全Gridの新集計は次Queueまで保留。p189 whole/p173全3,805件100%は未達。

## q1087-i01 有限部分範囲（2026-10-14 07:09 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（08:39 JSTまで）に適用。q1077と同一固定Grid testharness適格502 IDを、q1086後のhost browserで同一WPT/manifest/localhost/固定font3種/5秒settle/20秒process、最大128件ずつ4 shard再測定する。全ID/hash/checkpoint/gzip rawと旧outcome/subtest遷移、旧PASS回帰を照合し、残る共通失敗群を分類する。browser/WPT/runner/公開ABI/`plan/master.md`変更なし。90分以内に終わらなければ完了済みと未実行を分けてunclearedで止める。[Guardrail](../../guardrail.md)とrunner近傍規則を適用。p189 whole/p173全3,805件100%はこの部分受け入れに含めない。

## q1087-i01 結果（2026-10-14 07:30 JST）

Event ws074-p189-q1087-partial-clear-20261014: 固定Grid適格502 IDをq1086後のhost browserで全件同条件再測定。[証拠](evidence/20261014-q1087/README.md)。旧71 PASS/399 FAIL/32 no_output→新103 PASS/399 FAIL/no_output0。32 no_output→PASSのみ、旧PASS後退0。subtest PASS1877→5077（+3200）・FAIL5928維持。4 shard正常終了、ID集合/hash/checkpoint/gzip raw502照合。残るFAIL399はalignment206、grid-definition42、parsing35等。q1087-i01 partial cleared、p189 wholeとp173全3,805件100%は未達。browser/WPT/runner/ABI/Master無変更、共有投影/GitHub publication保留。

## q1093-i01 有限部分範囲（2026-10-14 07:51 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:21 JSTまで）に適用。q1087と同一固定Grid testharness502 ID、WPT commit `2d66b9b7998bb58c336138c178323ddee857b586`、localhost、3固定font、5秒settle/20秒processで4 shard再測定。raw/checkpoint/ID hashを検証し、q1087 whole outcome・subtest遷移・旧PASS後退と残FAIL群を分類。製品browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/静的reftest再測定なし。p189 whole1,783/p173全件3,805の100%は本部分基準外。

## q1093-i01 結果（2026-10-14 07:57 JST）

Event ws074-p189-q1093-partial-clear-20261014: [証拠](evidence/20261014-q1093/README.md)。固定Grid502件をq1087と同条件で全再測定し103 PASS/399 FAIL/no_output0。旧PASS後退0、whole outcome変化0。13ファイルの22 subtestがFAIL→PASS、PASS→FAIL0。全502 ID/hash/checkpoint/gzip raw一致。次は独立のGrid shorthand等の共通失敗を調査。q1093-i01部分cleared、p189 whole1,783/p173全3,805件100%未達。製品browser/runner/WPT/ABI/Master無変更、共有投影/GitHub publication保留。

## q1102-i01 有限部分範囲（2026-10-14 08:20 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:50 JSTまで）に適用。固定Grid style-changes001〜008の8 IDをq1099と同じWPT/localhost/固定font3種/5秒settle/20秒processで再測定し、q1099 rawからwhole/subtest遷移、旧PASS後退、残FAIL分類を記録。browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/全Grid再測定なし。p189 whole/p173全件100%は本部分基準外。

## q1102-i01 結果（2026-10-14 08:20 JST）

Event ws074-p189-q1102-partial-clear-20261014: [証拠](evidence/20261014-q1102/README.md)。固定style-changes8 IDを同条件再測定し4→6 whole PASS、26→36/48 subtest PASS。003/004計10 FAIL→PASS、旧PASS後退0。残007/008はvertical-lr文字子width期待20/40/30対実100。q1102部分cleared、p189/p217/p219/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1105-i01 有限部分範囲（2026-10-14 08:26 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（09:56 JSTまで）に適用。q1093固定Grid502 IDを同一WPT/localhost/固定font3種/5秒settle/20秒process・4 shardで全再測定。502 ID/hash/checkpoint/raw一致、q1093 whole/subtest遷移、旧PASS後退、残FAIL群を分類。browser/runner/WPT/公開ABI/`plan/master.md`変更なし、Acid/静的reftest再測定なし。p189 whole/p173全件100%は本部分基準外。

## q1105-i01 結果（2026-10-14 08:36 JST）

Event ws074-p189-q1105-partial-clear-20261014: [証拠](evidence/20261014-q1105/README.md)。固定Grid502件を同条件全再測定し103→110 PASS、399→392 FAIL、no_output0。7 whole FAIL→PASS、旧whole PASS後退0。subtestは127 FAIL→PASS・1 PASS→FAIL（既存whole FAILのfr/gutter `.grid 17` 幅期待200/実120）。全502 ID/hash/checkpoint/gzip raw一致。q1105部分cleared、p189 whole1,783/p173全3,805件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。
