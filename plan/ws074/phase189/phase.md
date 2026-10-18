<!-- awesome-plan project=zedbsd record=ws074-p189 -->

# ws074-p189: Interop 2025 testharness 結果取得

Status: uncleared（q24 corrected provider partial cleared、適格1508/未attempt275・yield*未実装/native holdback1）

## ws074-q20261018-03 結果

Event ws074-p189-q03-partial-clear-20261018: [証拠](evidence/20261018-q03/README.md)。1,455 unique ID/raw/checkpoint/hash照合、485 PASS、910 FAIL、16 harness_error、20 no_completion、24 no_output。q1202比481→485、旧PASS後退0。未対応328保持。部分cleared、whole uncleared。cleanup不足をp242へ渡す。共有投影pending。

## ws074-q20261018-03 有限範囲（2026-10-18）

[自走の指示](../autonomy.md)を最大90分で適用。固定1,783 testharness ID中、既存candidate_reasonに適格な1,455 IDを、最大128件ずつの12shardで測定する。固定WPT/metadata/manifest/font、settle5秒・process20秒、固定browser/libbrowserの組を使う。whole/subtest/raw callbackと全ID/checkpointを検証し、未対応328をPASS扱いせず理由付きで残す。runner/製品コードは変更せず、失敗群を後続Phaseへ渡す。binaryはp241の9b94ebe38と同じ出力。Master/共有Queue/GitHubのprojectionはpending。

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

Historical status（2026-10-14）: uncleared（q1013/q1014/q1015 partial cleared、whole1,783件は未達）。2026-10-18 Q1が先頭へ投影した旧状態を履歴として保持。
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

Design note ws074-p242-proposed-20261018: q03の途中で、anchor/@scopeのcleanupが`replaceChildren`未定義で後続subtestをNOTRUNにしている事実を確認。[p242](../phase242/phase.md)を後続のDOM一般機能の候補として追加。q03の測定とp189の受け入れは変更せず、baselineには現行binaryのharness_errorをそのまま記録する。

Event ws074-census-retention-find-20261018: q11 auditでcensus gzip後にraw JSONをunlinkする既存処理を確認。q11分はgzipから同じbytesへ復元して保持。次の有限Queueでunlinkを外し、actual one-ID report/raw/gzip equalityを確認してから再利用する。現在の測定値/圧縮raw/historyは変更しない。

Event ws074-p189-q12-i01-start-20261018: q11のauditを受けてcensus raw retentionを選定。gzip後に元JSONを削除せず両方を保持し、actual1 IDで同値を確認する。出力のcleanupはQ1管理。判定/固定ID/denominatorは変更しない。

Event ws074-p189-q12-i01-cleared-20261018: [証拠](evidence/20261018-q12-i01/README.md)。actual1 ID PASSでraw JSON/gzip/checkpoint/report同値、new delete無し。retention部分cleared、p189 whole未達。次のi02検証がこのrunnerを使う。共有投影pending。

Event ws074-p189-variant-readiness-20261018: q12中のreadonly分類で、variant80のunderlying sourceはplain candidate36/driver等43/HTTPS1。generated45のordinary window19中、source marker無し17/特殊2、残り26はworker等。次はまずplain variant36を有限Queueへ選定し、URL query保持・full selected ID照合・既存plain対照を検証する候補。window wrapperはpinned WPTのmetadata/realm/HTTP header semanticsを整えてから扱い、worker/HTTPS/driverを勝手に対象へ混ぜない。q12の1455分類は実行中のため変更しない。

Event ws074-p189-q12-full-census-checkpoint-20261018: [全1455 raw](../phase247/evidence/20261018-q12/README.md)。新retained runnerでbefore/after全件489 whole/11534 callback PASS、918 FAIL/3 harness_error/21 no_completion/24 no_outputを維持。328未対応は分母内に保持。q03よりwhole485→489だがq12 font由来増分0、既存q07/q10の4件。次のplain variant36候補まで分類は不変。

Event ws074-p189-q13-start-20261018: q12 verified output後、plain variant36のrunner対応を最大45分へ選定。pathでsource条件を評価しfull selected IDを保つ。旧1455維持/追加36のみ、global candidate1491/unrun292。43 driver+1 HTTPS variantは各理由へ、generated45は未対応。全36と5対照/actual query roundtripを検証。専用Python全文標準は未登録、既存tool形式を保ちproject-wide policyを導入しない。

Event ws074-p189-q13-partial-cleared-20261018: [証拠](evidence/20261018-q13/README.md)。追加36を全実行、34 FAIL/2 multiple_completions（initial autofocus error/14 NOTRUN）でPASS増分0。旧1455集合維持・候補1491/未対応292、actual query roundtrip/旧plain5対照同値、raw/hash/checkpoint欠損0。native engine/WPT/result classifier不変、Python syntax/diff通過。variant runner部分cleared、wholeは未達。generated/window・HTTPS/driver/workerとnative autofocus/navigation等が残る。共有投影pending。

Event ws074-p189-q14-census-checkpoint-20261018: [p250 final gate](../phase250/evidence/20261018-q14/README.md)で固定eligible1491全件491 PASS/950 FAIL/3 harness_error/21 no_completion/24 no_output/2 multiple_completions。old489 PASS維持、computed Grid2 whole改善、13 callback改善/16 animation偽陽性露出を記録。候補hash8eb1a5cacf64d34ab0b36d026b6f1335725ce9f9f886ba69b9c286bac8319727、全raw/progress/report一致・名前欠落0。runner自体は未変更でp189 whole未達/未対応292を維持。

## q21中の次候補調査: harness HTTP shutdown待機（未選定）

実環境Python3.13 socketserver.BaseServer.serve_foreverのdefault poll_interval=0.5はselector/shutdown確認周期であり、現在のharnessはtestごとにbrowser subprocess終了→server.shutdown→joinを行う。このpost-browser待機が全1491の反復コストになる。候補はWptServer workerのkwargsでpoll_interval=0.01へ限定し、localhost HTTP/response bytes/variant/結果分類/settle5000/process20/fonts/eligible IDを一切変えず、before=完了したq21全1491と同一frozen pairで全ID/whole/callback/order/欠損/追加を照合する最大45分の新Queue。native C/他runner/WPT/分母/未attempt292は不変。1行tool変更に専用mirror testは追加せず、実測before/after全1491と既存対照を使う。q21終了とexact Queue記録前には実装しない。native源不変なのでstatic/scripted/crashを再計測する必要はない。elapsedの改善は測定前に確定せず、差異があればscope内で診断し未解消ならuncleared。

Event ws074-p189-shutdown-candidate-20261018: q21 native全件gateの進行中にstdlib実sourceから停止待機を確認。次の有限candidateとして残し、現q21/global/native source/承認基準は変更しない。whole p189 coverage1783とnative/API未達を先取りclearしない。shared projection pending。


Event ws074-p189-q21-regression-evidence-20261018: [p252 global](../phase252/evidence/20261018-q21/README.md)で全1491は491→493 PASS、7 callback改善/旧whole・callback後退0/欠損・追加・順序差0。全1783中292未attemptは保持しp189 whole uncleared。次候補shutdown待機の短縮を同じfrozen pair/1491で選定するまでtool変更しない。shared projection pending。


## q22 exact scope / verification

Timebox: 最大45分（16:50 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-22-i01 | [p189](phase189/phase.md) harness shutdown partial | WptServer workerのserve_forever poll_intervalをPython既定0.5秒から0.01秒へ明示する1行変更のみ。browser終了後shutdown/select待機を短くし、HTTP内容・report callback追加・URL/query・分類・eligible1491/未attempt292・fonts・settle5000/process20を変えない。既存分類5対照、同じq21 frozen browser/libで全1491を実測し、q21全ID/whole/callback全status/順序/欠損/追加・membership hash一致とelapsedを確認、raw/gzip/checkpoint/commands/hashを全保存。意味のある差異が残ればuncleared。native C/SDK/WPT/batch/census/static/scripted/crash/基準/分母は変更しない | in-progress | q21 final全1491 493 PASS/11538 callback PASSとfrozen pair/1491 raw、stdlib3.13 serve_forever/shutdown実source |

Dependency graph: q21 history/baseline/pair → 1行poll設定 → 既存分類5 → 全1491実測/同値照合/elapsed → archive/清算。
Generation bounds: plan/ws074/tests/run-interop2025-harness.pyのThread kwargs1行とWS074記録/証拠だけ。Python専用full standard無し、Guardrail/既存tool・FS規則を直接適用。mirror testは追加せず、既存font fixtureのTemporaryDirectory cleanup試験は未選択、5つのpure分類対照を使う。新native source不変なのでstatic1561/scripted413/crash48/DOM/C/Acidはq21のbyte同一pair証拠を再利用し再実行しない。push/削除/make check/QEMU/共有board・cache/toolchain/sysrootを行わない。
Bounds: 全1491既存40分上限/Queue45分を守り、実測前に速度改善を主張しない。HTTPが使える既存escalated環境で実行し、未完/差異はraw/resumeとunclearedを残す。whole p189/p100/WS100%は先取りclearしない。
Upcoming Work Outlook: generatedWindow等の未attempt292と環境5、native animation/module/ViewTransition/anchor、p249/target/p100。

Event ws074-p189-q22-selected-20261018: q21のnative改善7/全旧PASS維持と全1491 rawをread-back後、調査済みshutdown待機の1行を選定。固定source/pair/URL・分類・virtual budgetは不変、実測全callback同値とwall elapsedを確認する。新たなproduct/APIの実装やrunner coverageを達成したとはしない。shared projection pending。


Event ws074-q22-finished-20261018: [exact history](../history/ws074-q20261018-22.md)・[証拠](evidence/20261018-q22/README.md)。harness HTTP serve_foreverの停止確認pollだけを0.5→0.01秒へ変更。既存分類5対照通過、同じq21 frozen pairで全1491を実測し全ID/whole/returncode/全callback・completion status/項目順/メッセージが完全同値。493 PASS・11538 PASS/20219 FAIL/14 NOTRUN維持、後退/改善/欠損/追加/順序差/メッセージ差0。実測920.9→226.8秒（4.0604倍、694.1秒短縮）。raw/gzip/checkpoint/全tar member/read-back、fixed corpus/font/source/pair hash一致。 i01 scoped partial cleared、p189 wholeの未attempt292/環境/API/全1783は未達。browser C/WPT/他runner/判定基準/分母は不変。確認済み1481/3805（38.922470%）、PASS増分0。static/scripted/crash/DOM/C/Acidはbyte同一pairのq21証拠を再利用し不要な再測定を省略。shared projection/GitHub/target full/p100/WS未達、push無し。


## q23 exact design / verification

Timebox: 最大90分（17:44 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-23-i01 | [p189](phase189/phase.md) official generated Window transport partial | pinned serve.pyのWindowHandler/AnyHtmlHandler wrapper literalをruntime ASTで読取り、sourcefile.pyと同じ先頭META連続block/global exposure/順序/escaping/script importsとquery/JS source pathを提供。pinned WebIDLParser.js→resources/webidl2/lib/webidl2.js internal GET rewriteをquery保持で提供。new generated contextだけでq20 verified static header provider/unsupported判定を再利用し、original testharnessreport.js bytes＋既存REPORT_SCRIPTを保持。元source/JS/resourcesとwrapper/headers/rewrite/hash/console binaryを記録、native APIや値は補完しない。既存physical1491のprovider/report/budgetは不変。primary byte/metadata/rewrite/HTTP比較、native Window/Any/context/meta-order/query/recording positiveとglobal/missing/unsupported/duplicate等のnegative controlsを確認 | in-progress | q22 same native pair/全1491 raw、pinned serve.py/sourcefile.py/server.py、q20 header source/29 controls、実17 input inventory |
| ws074-q20261018-23-i02 | [p189](phase189/phase.md) fixed generated17 + full1508 census partial | i01 verified output後、HTTPS/worker/.sub以外で既存未attempt292中plain .window.html2/.any.html15の17 IDをcandidateへ追加し同じfrozen pair/fonts/settle5000/process20で全1508を実測。旧1491 subsetの全ID/whole/callback/status/order/欠損/追加とq22比較、new17全native attempt/各source/wrapper/requests/unsupported/stdio/bin/report保存、eligible digest/全1783 partition/残275未attemptを照合。native未実装・server unsupportedは非PASSとして分母保持、gainsを実測前に加算しない | pending | i01 native/primary controlsと原bytes/headers/rewriteのverified output、q22 whole493/全1491 baseline |

Dependency graph: q22 history/pair/input inventory → i01 primary/native controls → i02全1508/old1491比較/archive。
Generation bounds: plan/ws074/tests/run-interop2025-harness.py、run-interop2025-batch.py、新test-interop2025-generated.pyとWS074記録/証拠だけ。Python専用full standard無し、Guardrail/既存tool/FS規則を直接適用。q20 scripted toolの既存Handler/static header/limit helperはgeneratedだけlazy利用し、scripted/crash/census既存tool/native C/SDK/expectations/manifest/共有board/cache/toolchainは変えない。private .headers/pipe/.sub/generated server/testdriver/HTTPS/worker/外origin等未対応は非PASS。wrapper source .headers自体はこの17で存在しないことをinventoryで確認し未対応なら拒否、通常resource headersはfaithful配信する。WPT rootファイルを追加/変更せずaliasはHTTP provider内部で処理、templateとassetのoriginal pinned bytesを使用。
Verification bounds: positive tool prerequisiteが欠ける/old subset後退が未解消/期限内未完ならaffected item uncleared、raw/resumeを保持。0 tests/invalid/multiple/failed fixtureをPASSにしない。17に全PASSを約束せず、未実装Native APIをJSで偽装/patchしない。native源不変なのでstatic/scripted/crash/DOM/C/Acidはq21 byte同一pair証拠を再利用し不要な再測定無し。push/削除/make check/QEMU無し。
Upcoming Work Outlook: 残るHTTPS/worker/sub/origin等275、native module/URLPattern/Wasm/performance/animation/ViewTransition/anchor、p249/target/p100。

Event ws074-p189-q23-selected-20261018: 固定未attempt292のactual plain Window17を具体化。pinned META parserは最初の非META行で終了し、Any window exposureの既定/first global、script/title/timeoutを原手順へ合わせる。WebIDLParserは原Gitに無いaliasで、同じpinned serve.py rewriteが既存125228-byte webidl2へ内部GET転送することを確認。Wrapper literalsは原sourceをruntime参照して原本をvendoringしない。既存q20 header実装をgeneratedだけ再利用し、physical1491は従来providerを保つ。whole p189/p173と未達/native APIは先取りclearしない。shared projection pending。


Event ws074-p189-q23-wrapper-output-20261018: primary/native13/13と既存pure分類5/5を確認。Window/Any/global/script順/query/currentScript/log、実HTTP headers/原body/WebIDL内部rewrite、native failure/unsupported static fallback・duplicate/zero境界を検証。fixture準備の型注釈/CSS参照/duplicate入力時点を訂正して再実行、元のnegative期待は保持。eligible1508=old1491+new17、残275未attemptをinventory/hashで確認。i01 scoped partial cleared→i02全1508実測開始、whole p189/WSは未達。shared projection pending。


Event ws074-q23-performance-prerequisite-redesign-20261018: [known holdback](evidence/20261018-q23/native-prerequisite-holdback.json)・[native/Chrome](evidence/20261018-q23/performance-prereq/probe.html)。追加17はnative全attempt、raw1 PASSはmark/observeが何もしない既存実装で試験入力が無く、確認済みへ加算しない。raw result/期待値は改変せず、coverage own scopeとNative API受け入れを分離して記録。[p254](../phase254/phase.md)実mark state→[p255](../phase255/phase.md)future callback/past suppression→本Phase再評価を追加し、新両Phase/WSへscope/resumeを配送。old1491結果は全status/order/messages不変、p077旧scopeのclearanceも維持。current q23範囲/期限/分母不変、まだterminal清算前。shared projection pending。


Event ws074-q23-finished-20261018: [exact history](../history/ws074-q20261018-23.md)・[証拠](evidence/20261018-q23/README.md)。pinned Window/Any wrapper・META・WebIDL内部GET rewrite・generated専用ordinary headersを提供し、primary/native13＋既存分類5対照通過。全1508=old1491+new17を同じfrozen pairで実測、old1491は全ID/whole/returncode/callback・completion status/order/messages完全同値。追加17は全native attempt、raw1 PASS/10 FAIL/5 no_completion/1 harness_error、426 callbacks（2 PASS/424 FAIL）。全1783の未attempt292→275、native C/WPT/期待値/分母は不変。 新1 raw PASSは既存performance.markの記録なし・getEntries空・PerformanceObserver.observe no-opで入力が欠落しており、native/Chrome独立probeでprior/future 0/0対1/1とfuture通知の差を確認。元WPT結果は改変せずknown native prerequisite holdback1として確認済みに加算しない。raw fixed1482/3805、確認済み1481/3805（38.922470%）維持。 両item scoped coverage partial cleared、whole p189/WS・native APIs・target full/p100は未達。[p254](../phase254/phase.md)実mark→[p255](../phase255/phase.md)実observer→p189 holdback再評価の依存を追加。p077旧readonly/empty-entry scopeの受け入れは保持。source/pair/fonts/manifest/全ID/hash、全raw/stdout binary/生成HTML/原JS/resources/header provenance/controls tarをread-back、WPT tracked clean。shared projection/GitHub pending、push無し。


Event ws074-q23-clearance-invalidated-idl-20261018: q23 terminal17cd21137後のauditで、event-timing/storage-accessの2 IDが正規Gitにあるinterfaces/*.idlを疎checkout不在で404とし、idl_test setupで失敗していた。[proof](evidence/20261018-q23-followup/missing-idl-proof.json)。generated資源の忠実性基準未達のためq23 i01/i02のclearanceをinvalidateしcurrent attemptをunclearedへ訂正。old1491同値/他15入力/原report・原Outcome・全artifactを保持、Native bug修正やfalsePASS holdbackの解除とはしない。確認済み1481は不変。新finite QueueでGit原本のmissing IDL GETを提供・独立wire/expected404を検証→2 ID/全1508再確認して再開する。p254 native producerはそのverified baseline後へ延期、まだ未実装。shared projection pending。

## q24 finite scope（2026-10-18）

Event ws074-q24-selected-20261018: [Queue](../queue.md) q24 i01 missing pinned IDL GET原bytes/provenance/metadata fidelity controls→i02 IDL2/全1508、最大60分17:51 JST。native変更無し、旧1491とholdback保持。q23訂正後の新attempt、whole acceptanceは未達。

Event ws074-q24-finished-20261018: [history](../history/ws074-q20261018-24.md)・[evidence](evidence/20261018-q24/README.md)。疎checkoutに無いinterfaces/*.idlをpinned Git原blobからgenerated専用GETで配信し、5種類/2 ID計6資源の原hash・query・MIME・header metadata不在と全rawを検証。独立wire/本物404/未配信Git資源nonPASS/metadata拒否を含む15 controls通過。全1508を235.8秒で再測定、全whole/status不変、IDL2以外1506の全callback/completion objectは完全同一（旧1491を含む）。IDL2のfetch errorは解消し、native JS compilerがyield*を未実装のため公式WebIDL2 parser実行に失敗する正しい残差へ到達。 raw harness494 PASS/958 FAIL/4 harness_error/26 no_completion/24 no_output/2 multiple、callback11540 PASS/20643 FAIL/14 NOTRUN。known native prerequisite holdback1は保持し確認済1481/3805（38.922470%）、未attempt275。native C/固定WPT/期待値/分母不変、static844/scripted105/crash39/DOM/C/Acidは同一q21 pairの証拠を再利用。 q24両scoped attempt cleared、q23元uncleared履歴は保持。whole p189/WS/p100/target fullは未達、shared projection/GitHub pending、push無し。次はp254 native mark producer、p255 observerでpositive通知を検証するまでheld PASSを確認済へ加算しない。yield*は次のVM候補としてp189に残す。

Event ws074-p255-primary-version-correction-20261018: [p255の一次文書日付訂正](../phase255/phase.md)を反映。entryTypes+他memberのpublished TypeErrorは2025-05-21 CRDにも存在するため、q23時の「2026 draftとの差」は単純な年次変更と断定しない。固定tentative WPT/Chrome挙動との差を保持し、実装scope選定で互換profileを記録する。q25 mark producerの原結果とheld PASS/whole判定は不変。

Event ws074-q25-finished-20261018: [history](../history/ws074-q20261018-25.md)・[evidence](../phase254/evidence/20261018-q25/README.md)。native PerformanceEntry/PerformanceMarkをprivate registryへ追加し、markの実記録/返値、constructor非buffering、readonly enumerable common fields/primitive detail/common4 toJSON、finite非負timestamp、required引数/予約名/例外、stable timestamp順のgetEntries/ByName/ByTypeとclearMarksを実装。native brandとmanaged payloadでprototype偽装を拒否し、receiverのactual relevant global/clock/prototypeを使用。38条件が独立Chromeと完全一致、buffer-only/saved entry/辞書Get・filter再入GC/retired child/cross-realm/allocation pressureのnative7条件通過。global公開前の既存helperのnewborn rootも補強。 最終source2d5db3785・frozen verified pairで全1508 harnessはq24比全whole/callback/completion/returncode完全同一、494 raw PASS維持。static1561は844 PASS/717 FAIL、全pixel metrics不変。scripted413は105 PASS/283 FAIL/5 unsupported/20 missing、805 captures全RGB不変。strict48は39 PASS/9 missing不変。DOM37/37・CSSOM12+12、host/target exact2 source warning0、Acid3得点/全inventory100・pixel差0、Acid2既存差873不変。 確認済1481/3805（38.922470%）維持、raw fixed1482。独立probeはprior/future entry各1に改善したがObserverはまだno-opで通知0、known held PASS1は解除しない。複雑detail clone/id/navigationId/measure/resource/event/workerは未実装、p254 wholeはuncleared、q25 host producer scoped itemだけcleared。p255はverified producer outputを前提に次のfinite Queueへ選定可能。p189未attempt275/p100/target full link/runtime/WS全件100%は未達、shared projection/GitHub pending、push無し。
