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
