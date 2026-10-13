<!-- awesome-plan project=zedbsd record=ws074-p190 -->

# ws074-p190: Interop 2025 reftest・crashtest runner coverage

Status: uncleared（2026-10-14 q1044 partial、全件coverage未達）
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

## q1043-i01 の有限部分範囲

2026-10-14 01:42 JST、ユーザーの事前承認を適用、最大90分。q1042で適格と確定した固定Interop静的reftest全1,561 IDを同一HTTP・800×600・固定font・q1034参照連鎖条件で全実測する。固定manifest1,974 reftest IDと適格1,561/未対応413の分母照合、focus別local PASS/FAIL/error、最初の代表差分画像と結果JSONを保存し、q1042広域100件の結果/画素差との一致も監査する。script関連407・variant6の実行、browser/WPT/公開ABI/`master.md`変更は含めない。90分内に終わらなければ測定済み件数と未測定件数を記録して終了する。結果JSON、ID集合、diff checkを検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1043-i01 結果（2026-10-14 01:46 JST）

**uncleared、p190 whole uncleared。** 固定静的1,561件の先頭380 IDまで実行し、105 local PASS/273 FAIL/2 error。2件のXHTMLで1件約60秒を要し、周辺caseも遅いため、90分timebox内の完了が危ぶまれた。runnerは最後にのみreportを書き、途中の参照画素差は保存しない。これ以上同じ実行条件に時間を費やさず中断し、先頭380 IDのstatusを[部分証拠](evidence/20261014-q1043/README.md)に保存。残り1,181 IDは未実測。次はcase別上限の短縮とcaseごとのdurable checkpointをrunnerへ追加し、全件を再実行する。browser/WPT/Master変更なし、host build/Acid再計測省略。p190 whole/p173全件100%未達、GitHub publication保留。

Event ws074-p190-q1043-uncleared-20261014: q1043-i01 uncleared。遅いXHTMLと最終reportのみのrunnerでは全件測定が再開しにくいため、q1044で有界case時間と途中保存を追加する。

## q1044-i01 の有限部分範囲

2026-10-14 01:47 JST、ユーザーの事前承認を適用、最大90分。q1043は60秒上限の重いXHTMLで遅延し、runnerが最後にしかJSONを書かないため380件の画素差が失われた。static runnerにcaseごとに指定可能なrenderer timeoutを追加し、test/reference/chainの各renderへ適用する。全選定/環境情報と各結果を完了時にfsyncしたJSONLへ保存し、異常中断でも測定済みmetricsを残す。固定静的1,561 IDをcase別5秒で再実行し、q1042広域100件の非timeout結果との一致、q1043遅延2 IDの明示的timeout、focus別結果/分母を検証する。短い上限によるtimeoutはPASSに数えず、後続調査対象とする。script/variant/公式環境同等性、browser/WPT/公開ABI/`master.md`変更は含めない。局所timeout/checkpoint control、Python compile/diff checkを行う。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

## q1044-i01 結果（2026-10-14 01:55 JST）

**partial scope cleared、p190 whole uncleared。** runnerにcase別描画上限と逐次fsync JSONLを追加し、固定静的1,561 IDを全実測。[結果・checkpoint](evidence/20261014-q1044/README.md): 500 local PASS/1,051 FAIL/10 timeout/0他error。未対応413 IDはscript402/scripted reference5/variant6。timeout10件はflexbox justify-content XHTMLに集中、q1043の遅延2 IDも明示的timeout。q1042広域100件はstatusと参照画素差が全件一致。JSONL1,561結果のindex/選定hashとfinal report一致、selection3/3+chain4/4+checkpoint2/2、Python compile/diff check PASS。browser/WPT/Master無変更、host build/Acid再計測省略。公式環境同等性、scripted/variant対応と描画FAILは残り、p190 whole/p173全件100%未達、GitHub publication保留。

Event ws074-p190-q1044-outcome-20261014: q1044-i01 partial cleared、p190 whole uncleared。固定静的1,561 reftestのlocal baselineを完了。10 timeoutと1,051 FAIL、413 runner未対応を次の調査へ分ける。

Event ws074-p190-q1045-p195-link-20261014: q1044のtimeout10 IDを新設[p195](../phase195/phase.md)の一般欠陥調査へ渡す。p190のrunner coverage/公式条件の受け入れは変えず、p195の修正結果を次の再測定で使用する。GitHub publication保留。

Event ws074-p190-q1046-p196-output-20261014: [p196](../phase196/phase.md)の最上位XHTML対応後、固定timeout10 IDを同じ5秒で再測定し0 timeout・1 local PASS/9 FAIL。[証拠](../phase196/evidence/20261014-q1046/README.md)。p190全静的1,561件のq1044 baselineは履歴として保持し、次の全件再測定が最新baselineを作る。script/variantと公式環境同等性は未達、p190 wholeはuncleared。

Event ws074-p190-q1047-p197-output-20261014: [p197](../phase197/phase.md)の横方向負余白修正後、固定10件を同じ条件で測定し3 local PASS/7 FAIL/0 timeout。横方向2 IDのみFAIL→PASS、先行PASSは維持。[証拠](../phase197/evidence/20261014-q1047/README.md)。全静的1,561件再測定と残りrunner coverageは未実施、p190 wholeはuncleared。

Event ws074-p190-q1048-p198-output-20261014: [p198](../phase198/phase.md)のfloat BFC margin相殺修正後、固定10件3 local PASS/7 FAIL/0 timeoutで数は不変。vert001a/006差画素減、vert001b差画素増を[証拠](../phase198/evidence/20261014-q1048/README.md)に保持。全静的1,561件再測定/残りrunner coverage未実施、p190 wholeはuncleared。

## q1053-i01 の有限部分範囲

2026-10-14 02:37 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:07 JSTまで）。q1044の固定manifest・静的適格1,561 ID・同一HTTP・800×600・固定font・case別5秒・逐次fsync checkpoint条件を維持し、q1046〜q1052のXHTML/Flex修正後を全件再測定する。q1044との同一IDでPASS/FAIL/timeout遷移とfocus別集計を照合し、改善と回帰を分けて記録する。413未対応IDは未実行理由を維持。browser/WPT/公開ABI/`plan/master.md`の変更、scripted/variant/HTTPS runner追加は含めない。runが90分内に終わらない場合はcheckpointと未測定件数を記録しunclearedで終了する。結果JSON・ID集合・diff checkで検証する。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。

Event ws074-p190-q1053-design-20261014: q1052で固定Flexbox10件が10/10 local pixel exact PASSになったため、q1044静的全件baselineを同条件で更新し、次の一般欠陥を件数で選ぶ。p190の全2,022件coverage基準とp173全3,805件分母は不変。GitHub publication保留。

## q1053-i01 結果（2026-10-14）

**partial scope cleared、p190 whole uncleared。** 固定静的適格1,561 IDを全件再測定し、旧baselineと同一ID/条件を照合。518 local PASS/684 FAIL/359 error/0 timeout。旧500/1,051/0/10から、Flex XHTML timeout10件は全てPASS。39 FAIL→PASS、8 PASS→FAIL、23 PASS→error、336 FAIL→error。全359 errorはDOCTYPEを含むXHTMLで、q1046以降のXML parserがDTDをENOTSUPにする一般条件に一致。[結果・遷移・focus集計](evidence/20261014-q1053/README.md)。checkpoint1,561行一致、固定ID1,561・未対応413分類一致、diff check PASS。browser/WPT/runnerコード無変更でhost build/Acid再測定省略。p190全2,022件coverage・p173全3,805件100%未達。Event ws074-p190-q1053-outcome-20261014: q1053-i01 partial cleared、p190 whole uncleared。DOCTYPE負荷の読込error修正と旧PASS→FAIL8件の再監査が次の条件。GitHub publication保留。
