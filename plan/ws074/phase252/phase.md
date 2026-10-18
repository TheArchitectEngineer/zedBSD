<!-- awesome-plan project=zedbsd record=ws074-p252 -->

# ws074-p252: flex-basisのgrammar/computedとnon-replaced content base

Status: in-progress（q21 scoped host attempt）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: [ws074-q20261018-21](../queue.md) / i01

## Existing evidence / own goal

q12/q13 baselineの固定flex-basis-computedはcontent→auto、負のabsolute calc未clamp、zero%型喪失の3 callbackがFAIL。invalidはnone/negative px/negative %の3 callback FAIL。validは全PASS。zero%型は[p251](../phase251/phase.md)の検証待ちoutput、残るgrammar/computedと[p240](../phase240/phase.md)のnon-replaced content vs autoの残りを候補にする。既存fixed3805集合にcomputed/invalid/validの3 IDが含まれることをrawで確認。

Own criteria候補: 既存accepted flex-basis keyword/length grammarを保持しcontentをprivate unitとして区別、none/negative literalを拒否、計算結果のnonnegative rangeをcascadeで扱う。通常computed getterでcontent/0%/rangeを返す。横書き非置換row/columnのcontent baseはpreferred main sizeを代用せず実contentから測り、measure時のstyle/cacheを全error pathでも復元する。aspect-ratioのdefinite cross転送は既存p241のoutputを使い、min/max clampとauto minimumをbase測定から区別する。

## Design / constraints

private CSS unit/parse/cascade/computed、flex baseのbounded helper。公開ABI/Wayland/shell/WPT/runner/結果判定/Master不変。新pointer ownershipは不要。rowのmax-content cacheがpreferred width/minmax下の古いmeasureを使わないこと、columnはpreferred heightを無視しcross widthを保つことを確認してから生成する。replaced/縦書き/一般intrinsic basis等の未達をこの非置換scopeの達成に混ぜない。

Guardrail・C全文・browser component全文を適用。生成scopeの全文規約と既存legacy残差を区別し、p100の全WS source関門を維持。新helperの宣言/definition/ANSI C/コメント/単純error flowをレビューし、既存大関数へ変更が必要ならgeneration scopeと規約検証を選定時に明記する。

## Proposed verification / resume condition

q14 final pairの全件gate後にexact Queueを選定するまで実装しない。最大90分候補。独立Chromiumでauto/contentとexplicit px/% main、row/column、frame/box-sizing、min/max、nowrap/wrap、ratio cross、mutation/measurement cache、literal拒否/absolute・percent・mixed calc rangeを確認。固定3 IDの全callback PASS、shorthand等のold PASS維持。固定harness1491とstatic1561のq14 verified baselineを使いold whole PASS後退0/全callback遷移診断、own flex-basis後退を解決。補助aspect257を実行する場合は固定3805の達成へ加算しない。DOM/CSSOM、host/target warning0、C全文/format19、Acid非増加を確認。target全体link/runtime/p100は後段の未達として保持。

Event ws074-p252-plan-from-existing-basis-residual-20261018: q14全件比較待ちの間にp240/p241の残りと固定rawを確認し、次の候補を具体化。新Queue未選定であり実装permission/clearanceを先取りしない。p240/p241既存scope/history/未達は変更しない。

## Standards evidence（2026-10-18 read-only）

[Flexbox §9.2](https://drafts.csswg.org/css-flexbox-1/#algo-main-item)（Editor’s Draft 2026-05-08）で、content basisとdefinite cross/ratioの順序、contentをmax-contentとして測る経路、base測定時にmain min/maxを適用しない点を確認。[CSS Values §10.12](https://drafts.csswg.org/css-values-4/#calc-range)でmath rangeを参照する。固定2025 WPT revision/期待値は維持し、新しいwidth grammarをこの候補へ無断追加しない。box-sizingのnegative inner baseや一般intrinsic sizingの既存未達を修正済みとはしない。

Event ws074-p252-q14-output-ready-20261018: q14 final88713badd/WIP98acae7a6でzero% callbackが改善し、固定flex-basis-computedにはcontent/negative calcの2 FAILが残る。q14 prerequisite outputはverified。次の選定は未確認crash48の正式条件をp190で優先するため本Phaseはplanned/未選定を保持し、その後候補へ。scope/criteriaは不変。


## q21 exact design / verification

Timebox: 最大90分（17:14 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| ws074-q20261018-21-i01 | [p252](phase252/phase.md) host flex-basis partial | private content unitでcontentをautoから区別。既存accepted length/intrinsic/autoを保ちnone/normal/negative literalを拒否、absolute calcのnonnegative computed rangeをcascadeへ適用しpercentage型/offsetを保持。横書き非置換row/columnでcontent basisをpreferred main/minmaxから分離して測定しstyle/main cache/tree measuring/float contextを全error pathで復元。既存definite cross ratio outputとmin/max hypothetical clampを維持。独立Chromium geometry/grammar/computed、既存fixed parsing3全callback、DOM/CSSOM/CSSOM-mutation、host/target exact single-source warning0、変更関数full C/clang-format19/style0、Acid非増加で確認。最終frozen pairで1491 harness/static1561/strict48/scripted413をq19/q20全ID/callback/pixelと照合しold whole491/839/39/105後退0と全遷移診断、raw/hashを保存 | in-progress | p251 zero-% output88713badd、p240/p241 native ratio output、q19 nativeXML/MO output1c6991441、q20 faithfulheaders/全1474 verified |

Dependency graph: q20 history/source proof → p252 producer → independent controls/parsing → final pair全件/清算。
Generation bounds: userland/desktop/libbrowser/css/css.h・values.c・cascade.c、bind/computed.c、layout/flex.c、およびWS074 tests/recordsだけ。既存巨大dispatcherはhelper呼出し/単純caseだけを変更、new helper/変更したcomputed_length/flex_base_sizeは全文規約で生成レビュー。private enum append以外のAPI/layout header/SDK/Wayland/shell/他engine/WPT/runner判定/共有board/cache/toolchain/sysroot不変。新memory ownership無し。replaced/縦書き/general intrinsic/negative inner basis/nonlinear calc既存近似/fullWS/target link/runtimeは達成に混ぜない。
Verification bounds: native source changedなので同一final pairで全eligible/strict/scriptedを再実行。未対応292/環境5/native未達は分母保持。finite native controlsと全件gateを期限までに完了できない/後退未解消ならunclearedとraw/resumeを残す。条件/期待値/分母を緩めない。push/削除/make check/QEMU無し。
Upcoming Work Outlook: harness generated window・HTTPS/worker/origin未attempt292、driver action/生成環境5、native animation/module/ViewTransition、p249 margin、p100。

Primary standards: Flexbox §9.2とValues §10.12を2026-10-18に再確認。content measurementはmain preferred/minmaxを一時auto/noneにして自分のmax-content cacheを退避・復元し、ratioのdefinite cross shortcutを先に保持。percent calcのnonnegative rangeはused解決後に既存base floorを適用、computed coefficientは壊さない。autoが参照するmain sizeとexplicit contentの区別を独立対照する。巨大dispatcher全部の既存legacyを修正済みとはしない。

Event ws074-p252-q21-selected-20261018: q20 archive/history read-back後に既存planned outcomeを選定。p240/p241のratio outputとp251 percentage metadataを消費し、各Phaseへ自身の依存/再開を記録。


Event ws074-p252-q21-preflight-20261018: geometry24とcomputed13はChromium一致。指定値calc(10px - 0.5em)はnativeが元順、Chromeはcalc(-0.5em + 10px)へcanonicalizeする既存差をraw保存。本scopeのgrammar acceptance/computed/layoutと既存fixed3全callbackは維持、独立fixtureの指定値欄は全値をaccepted Booleanで検証しcanonical serializerの既存未達を別記する。pure negative percentageはChromeとValues §10.13でcomputed0%を確認し、offset0時だけcoefficient clampする内部設計に訂正。mixed percentage/offsetの保持・used非負チェックは不変。旧raw/q21 acceptanceを改竄しない。

Event ws074-p252-q21-native-checkpoint-20261018: source WIP063d87c95/frozen final pair。独立Chrome39行= native、DOM36/36・CSSOM12/12・mutation12/12・既存ratio40/40、変更5 functions全文/format19/style0、full-file legacy count不増、host/target4 source warning0。全error pathでstyle/cache/measuring/context復元をmanual確認。最初のHTTP試験はsandbox socket EPERMで無効、rawを残し既存escalated環境の別verified出力へ再実行中。Acid3 score/inventory100/pixel0維持、固定1491/static1561/strict48/scripted413のterminal gate完了までpartial clearanceは判定しない。既存指定calcのcanonical順序差を修正済みとしない。shared projection pending。
