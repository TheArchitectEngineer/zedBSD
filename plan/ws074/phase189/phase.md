<!-- awesome-plan project=zedbsd record=ws074-p189 -->

# ws074-p189: Interop 2025 testharness 結果取得

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
