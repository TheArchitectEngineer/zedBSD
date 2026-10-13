<!-- awesome-plan project=zedbsd record=ws074-p190 -->

# ws074-p190: Interop 2025 reftest・crashtest runner coverage

Status: in-progress（2026-10-14 q1034 partial）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p173 q1012 manifest](../phase173/evidence/20261013-q1012/manifest.json) の固定選定出力。p189とは独立。

## 目的と受け入れ

Interop 2025 の reftest 1,974件と crashtest 48件を WPT の期待と環境条件どおりに実行し、PASS/FAIL/error/crash/timeout/未実行を分けて保存する。q1012時点の static reftest候補1,561件、scripted reftest407件、variant URL6件、crashtest48件をすべて対象に保持する。必要な同一/異origin、HTTPS、script待機、reference・fuzzy 条件を確認する。全2,022件のrunner coverageと再現可能な結果が whole clearance 条件。ブラウザ固有の実際の描画FAILは runner の不備と分けて後続改善Phaseに渡す。

## 再開

最初の Queue は q1012 の sample失敗が runner の誤判定か描画差かを代表画像と reference 条件で検証し、static候補の有限 shard を正式 selection ID と結びつける範囲で定義する。動的/HTTPS/crashtestを一度に実装するとは約束しない。コード・実行は exact-scope Queue 承認後のみ。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。`master.md` は変更しない。

Event ws074-p173-q1012-split-20261013: [p173](../phase173/phase.md) から testharness 以外のrunner coverageを分離。現行runnerの script除外や単一HTTP origin、`--selection-manifest` で確認した限定を全件baselineに残さないためのPhase。p173の受け入れ・公式選定は変更しない。GitHub publication保留。

## q1033-i01 の有限部分範囲

2026-10-14 01:20 JST、ユーザーの事前承認を適用、最大90分。固定manifestで選ばれた`html/rendering/the-details-element`の静的reftest候補を最大10件、既存`run-wpt-reftests.py`で同一HTTP・800×600・固定font条件で実測し、PASS/FAIL/errorとmatch/mismatch参照・画素差を監査する。代表PASS/FAILの誤判定要因を画像とmetadataに照らして記録する。scripted/variant/HTTPS/crashtestとbrowser/WPT/公開ABI/`master.md`変更は範囲外。検証は固定commit/manifest、reportと参照の整合、diff check。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

## q1033-i01 結果（2026-10-14 01:20 JST）

**partial scope cleared、p190 whole uncleared。** 固定選定10件の既存runner結果は4 provisional PASS/6 FAIL/0 error。[監査と画像](evidence/20261014-q1033/README.md)。`details-display-type-001.html`のPASSは参照がさらに別の参照へmatchする連鎖をrunnerが無視したfalse passと判明。各ID/参照/commitと画素差を確認。scripted/variant/HTTPS/crashtestは未実行。browser/WPT/Master無変更、diff check PASS。コード無変更のためhost build/Acid再計測省略。次は参照グラフを扱うrunner修正が必要。p190 wholeとp173全件100%未達、GitHub publication保留。

Event ws074-p190-q1033-outcome-20261014: q1033-i01 partial cleared、p190 wholeはuncleared。PASSのreference-chain誤判定を特定し、既存runnerの全件拡大前に修正する。

## q1034-i01 の有限部分範囲

2026-10-14 01:23 JST、ユーザーの事前承認を適用、最大90分。固定WPTの`tools/wptrunner/wptrunner/executors/base.py`はreftestのreference treeを末端まで辿る。現行runnerは直近1辺で止まり、q1033で`details-display-type-001.html`をfalse PASSとした。このQueueで静的な`rel=match`のみの参照連鎖を最大16辺まで辿り、途中一致だけではPASSにしない。cycle、欠損、scripted、match/mismatch混合の連鎖は結果をerror/unsupportedとして明示し、誤PASSを避ける。局所graph試験、q1033固定10件同条件再計測、runner Python compile/diff checkを行う。browser/WPT/公開ABI/`master.md`は変更しない。適用は[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

## q1034-i01 結果（2026-10-14 01:24 JST）

**partial scope cleared、p190 whole uncleared。** 静的match-only参照連鎖を最大16辺まで辿るrunner修正と4件の局所graph controlを完了。固定10件を同条件で再計測し、3 PASS/7 FAIL/0 error。q1033のfalse PASSは参照連鎖の2辺目4,930画素差によってFAILとなり、他9件の結果は維持。[証拠](evidence/20261014-q1034/README.md)。Python compileとdiff check PASS。browser/WPT/Master変更なし、host build/Acid再計測省略。scripted/variant/HTTPS/crashtest、完全なfuzzy override・混合reference graphは残り、p190 wholeとp173全3,805件100%は未達。GitHub publication保留。

Event ws074-p190-q1034-outcome-20261014: q1034-i01 partial cleared、p190 whole uncleared。途中一致のfalse PASSを解消。今後のrunner coverageは残余条件と固定選定の拡大を要する。

## q1037-i01 の有限部分範囲

2026-10-14 01:31 JST、ユーザーの事前承認を適用、最大90分。q1034の静的match-only参照連鎖対応runnerで、固定Interop manifestが選んだ `html/rendering/the-details-element` 内の静的reftest候補全件を同一HTTP origin・800×600・固定fontで実測する。固定details reftest25件とIDを照合し、PASS/FAIL/errorと未実行理由、参照連鎖・fuzzy/代表画像差を分類し、後続修正の優先対象を記録する。scripted/variant/HTTPS/crashtestの実装、browser/WPT/公開ABI/`master.md`変更は含めない。固定commit/manifest、結果JSONの件数・ID、diff checkを検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1037-i01 結果（2026-10-14 01:31 JST）

**partial scope cleared、p190 whole uncleared。** 固定details focusのうち `html/rendering/the-details-element` 内のreftest全21件をq1034 runnerで実測し、12 local PASS/9 FAIL/0 error。[結果・分類・代表画像](evidence/20261014-q1037/README.md)。FAILはreference-chain2件、`::details-content`5件、revert1件、inline layout1件。focus残り4 reftestはhidden属性directoryにあり、未実行として明示。ID/固定commit整合とdiff check PASS。コード無変更のためhost build/Acid再計測省略。p190 whole/p173全件100%未達、browser/WPT/Master無変更、GitHub publication保留。

Event ws074-p190-q1037-outcome-20261014: q1037-i01 partial cleared、p190 whole uncleared。固定details静的21件を測定し、残る4 reftestと3 crashtestのcoverageを分けて記録。

## q1038-i01 の有限部分範囲

2026-10-14 01:32 JST、ユーザーの事前承認を適用、最大90分。q1037で別directoryとして未実行とした固定details focusの `html/editing/the-hidden-attribute` 内4 reftest IDを、q1034 runnerの同一HTTP origin・800×600・固定font条件で実測する。q1037の21 IDと合わせた全25 reftestのID/状態を照合し、PASS/FAIL/error/未対応と画素差・参照条件を記録。runner条件外なら理由を明記して止める。crashtest/scripted/variant/HTTPSのrunner実装、browser/WPT/公開ABI/`master.md`変更は含めない。固定commit/manifest、JSON整合・diff checkを検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1038-i01 結果（2026-10-14 01:32 JST）

**partial scope cleared、p190 whole uncleared。** 固定hidden directoryの4 reftest ID中、static条件の004/005を実測して0 local PASS/2 FAIL/0 error。001/007はscriptとreftest-waitがあるため未実行。[結果・画像](evidence/20261014-q1038/README.md)。q1037の21件と合わせてdetails focus25件中23件測定、12 local PASS/11 FAIL/2未実行。004/005はhidden=until-foundのcontent visibility/size containment欠落が共通原因候補。ID/固定commit/参照とdiff check PASS。コード無変更のためhost build/Acid再計測省略。p190 whole/p173全件100%未達、browser/WPT/Master無変更、GitHub publication保留。

Event ws074-p190-q1038-outcome-20261014: q1038-i01 partial cleared、p190 whole uncleared。固定details focus25 reftestの23件をlocal static runnerで実測済み、scripted2件が未実行。

## q1039-i01 の有限部分範囲

2026-10-14 01:33 JST、ユーザーの事前承認を適用、最大90分。固定details focusのcrashtest3件はまだ未実行。固定manifestを確認し、単一HTTP・plain HTMLでrendererを起動する最小runnerを作る。exit0かつ画像生成をlocal pass、非0終了/crash signal、process timeout、画像欠損を分け、stdout/stderr/command/hashを記録する。分類control（成功・非0・timeout・画像欠損）、固定3 IDの実測、Python compile/diff checkを行う。scriptedページもbrowserの通常ロードは許すが、cross-origin/HTTPS/外部driver条件と公式WPT runner同等性の全検証は含めない。browser/WPT/公開ABI/`master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1039-i01 結果（2026-10-14 01:35 JST）

**partial scope cleared、p190 whole uncleared。** 固定manifest検証付き単一HTTP crashtest runnerを追加し、details3 IDは全て画像を生成してexit0、3 local PASS/0 crash/0 timeout/0 no-image。[証拠](evidence/20261014-q1039/README.md)。成功・非0・signal・timeout・画像欠損の分類control5/5、Python compile/diff check PASS。残り45 crashtestと条件差は未測定、公式WPT runner同等性も未確認。browser/WPT/Master/公開ABI無変更、host build/Acid再計測省略。p190 whole/p173全件100%未達、GitHub publication保留。

Event ws074-p190-q1039-outcome-20261014: q1039-i01 partial cleared、p190 whole uncleared。details crashtest3件のlocal renderer実測を得た。

## q1040-i01 の有限部分範囲

2026-10-14 01:35 JST、ユーザーの事前承認を適用、最大90分。q1039 runnerに全focus選択を追加し、固定manifestのcrashtest全48一意IDを800×600・固定font・単一HTTP・3秒settle・process timeout条件で一回ずつ測る。manifest/件数/分野分布、local PASS、crash signal、timeout、process error、画像欠損、unsupportedを区別し、q1039 details3件の結果と照合する。異origin/HTTPS/testdriver対応やbrowser描画機能修正、WPT/Master/公開ABI変更は含めない。runner選択control、Python compile、結果JSON/diff checkを検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1040-i01 結果（2026-10-14 01:36 JST）

**partial scope cleared、p190 whole uncleared。** 固定manifestのcrashtest48一意IDを全実測し、48 local PASS/0 crash/0 timeout/0 process error/0 no-image/0 unsupported。[証拠](evidence/20261014-q1040/README.md)。全IDの800×600画像を検証し、q1039 details3件は画像hashとoutcomeが一致。分野分布・固定commit・ID集合を照合。選択/分類control6/6、Python compile/diff check PASS。browser/WPT/Master無変更、host build/Acid再計測省略。これはlocal host条件のcrashtest測定であり公式WPT環境同等性やreftest/testharnessを証明しない。p190 whole/p173全件100%未達、GitHub publication保留。

Event ws074-p190-q1040-outcome-20261014: q1040-i01 partial cleared、p190 whole uncleared。固定48 crashtestのlocal測定を完了、reftest全件と公式環境同等性が残る。

## q1041-i01 の有限部分範囲

2026-10-14 01:37 JST、ユーザーの事前承認を適用、最大90分。固定Interop manifestのanchor-positioning focusに属するreftest77 IDのうち、既存q1034 static runnerが実行できる`css/css-anchor-position`の全候補を800×600・固定font・単一HTTPで実測する。残りの選定IDはscript/variant/参照/別directory等の理由をID単位で照合し、PASS/FAIL/errorと参照・代表差分を記録する。runnerやbrowser/WPT/公開ABI/`master.md`変更は含めない。固定commit/manifestと結果ID/件数、diff checkを検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1041-i01 結果（2026-10-14 01:37 JST）

**partial scope cleared、p190 whole uncleared。** 固定anchor-positioning reftest77 ID中、静的適格21件を実測し0 local PASS/21 FAIL/0 error。差画素1,665～80,000、中央値10,798。[結果・選定ID別未実行理由・代表画像](evidence/20261014-q1041/README.md)。残り56 IDはscriptを含むため未実行。固定commit/manifestとID集合、diff check PASS。コード無変更のためhost build/Acid再計測省略。anchor-positioning描画の一般欠陥とscripted runner条件は今後の別範囲。p190 whole/p173全件100%未達、browser/WPT/Master無変更、GitHub publication保留。

Event ws074-p190-q1041-outcome-20261014: q1041-i01 partial cleared、p190 whole uncleared。anchor-positioning固定77 reftestを21静的FAIL/56 scripted未実行に分類。

## q1042-i01 の有限部分範囲

2026-10-14 01:39 JST、ユーザーの事前承認を適用、最大90分。現行static runnerは広いWPT treeを走査してから選定IDで絞る。固定manifestのreftest ID/sourceを直接走査し、variant・script・参照欠損等を選定IDごとの未実行理由としてreportに残す。既存details/anchor静的結果との同一性、variant/重複を含む局所selection control、全分野から最大100静的IDの決定的sampleを同条件実測する。全1,974件実行、scripted/HTTPS対応、browser/WPT/公開ABI/`master.md`変更は含めない。Python compile、ID/件数/結果JSON/diff checkを検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1042-i01 結果（2026-10-14 01:41 JST）

**partial scope cleared、p190 whole uncleared。** static runnerが固定manifest選定sourceのみを走査し、選定reftest1,974 IDを静的適格1,561、script402、scripted reference5、variant6に一意分類。[証拠](evidence/20261014-q1042/README.md)。局所selection3/3・既存chain4/4、固定details21/anchor21の全結果/画素差が先行報告と一致。全分野focus round-robin 100件sampleは28 local PASS/72 FAIL/0 error。Python compile/ID分母/diff check PASS。残り1,461静的適格IDと413未対応IDは未実行。browser/WPT/Master無変更、host build/Acid再計測省略。p190 whole/p173全件100%未達、GitHub publication保留。

Event ws074-p190-q1042-outcome-20261014: q1042-i01 partial cleared、p190 whole uncleared。全reftest選定分母を高速に分類し、広域100件のlocal baselineを得た。
