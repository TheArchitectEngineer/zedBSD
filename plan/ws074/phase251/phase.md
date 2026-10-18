<!-- awesome-plan project=zedbsd record=ws074-p251 -->

# ws074-p251: linear calcのzero percentage type

Status: cleared（q14-i01 own criteria、2026-10-18）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-14-i01

## Goal / exact scope

[CSS Values 4 calculation typing](https://drafts.csswg.org/css-values-4/#calc-type-checking)とChromium参照で、calc(0% + 10px)/calc(5% - 5% + 10px)はcomputed valueでもpercentageを保持する。css_calc_sumは係数だけを保存しcascade_calcがpercent!=0だけで単位を決めるため、その型が失われる。direct linear SUMについてpercentageの存在をscalar flagで保持し、加減算・scalar積で係数0になっても型を維持する。

## Design / boundary

css_calc_sumへdirect percentage termのflagを追加、percentage token生成とsum加算で伝える。既存scale/struct copyはflagを維持。cascade_calcのSUMは、nested nonlinear node無しのlinear sumでこのflagを見てCSS_UNIT_PERCENT/offsetへ出力する。nested min/max/clampのviewport近似をこの変更で完成したとはしない。parse ownership/new pointer/公開ABI/Waylandは不変。

## Verification / criteria

q12/q13同じfrozen pairの既存1455+variant36 rawを1491 ID baselineとして照合し、old whole489 PASSを維持、callback全遷移を記録してown linear typingの後退を解決する。全static1561のold839 PASS維持、独立0%/cancelled%/scalar/nested ordinary calc・computed minmax/flex-basis/fit-boundをChromium比較、DOM/CSSOM/host target warning0/C全文format/Acid非増加。p250はこのverified outputを使ってfit computed criteriaを満たす。最大60分のq14に共存し、調査が枠を超えるならoutcome/resumeを残す。full source/target/WSはp100と後段。

Event ws074-p251-created-from-fit-diagnosis-20261018: p250生成前の17条件Chrome参照でzero/cancelled percentageの既存calculator依存を確認し、別Phaseへ分離。q14をuser autonomyで2itemへ改訂し、初期snapshotをp250 evidenceに保持。known animation false PASSはp248へ記録し、全callback count非減少は要求しない。

Event ws074-p251-scoped-output-prerequisite-20261018: p250のprerequisiteはこのPhaseの独立computed/native type preflightというscoped output。sourceは単独executorが順に変更し、両sourceを含むfinal pairのglobal1491/static1561で各itemのterminal gateを確認する。i01 clearanceを先取りしない。

Event ws074-p251-preflight-20261018: frozen type-preflight pairでdirect/cancelled/scalar/nested ordinary calc等10行・20 computed値がChromiumと完全一致。native engineの実computed queryで確認。全体gateはfinal pairへpending、whole clearanceは未判定。p250のscoped prerequisite outputを確認しi02実行へ。

Checkpoint ws074-p251-q14-final-static-20261018: 88713baddの初期化修正後final frozen pairで全static1561を完走。839 PASS/722 FAIL、old PASS後退0・全reference画素差count変化0。fixed flex-basis-computedのzero% callback改善もraw確認、content/negative calcの既存2 FAILはp252候補へ。full1491 terminal gateは継続中で、item/Phase clearanceは未判定。

Event ws074-p251-q14-cleared-20261018: [証拠](../phase250/evidence/20261018-q14/README.md)。source88713badd final pairで全harness1491 whole489→491・old PASS後退0、callback13改善/16旧animation偽陽性露出・欠落/追加/名前順変化0、全static1561 whole839維持・pixel count変化0。独立20 computed/4実寸・fit17行Chromium一致、DOM33/33/CSSOM各12/12、host/target warning0/C全文/format・Acid非増加。q14既定のown criteriaを満たしPhase/item cleared。native fit cap/nonlinear math/animationの未達をclearせず、p100/target全体/共有remote closureはpending。初版353件の停止診断は保持、terminalは初期化修正後の全件測定。
