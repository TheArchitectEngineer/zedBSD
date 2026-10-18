<!-- awesome-plan project=zedbsd record=ws074-p252 -->

# ws074-p252: flex-basisのgrammar/computedとnon-replaced content base

Status: planned
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（q14後の候補）

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
