<!-- awesome-plan project=zedbsd record=ws074-p250 -->

# ws074-p250: computed fit-content track bound

Status: cleared（q14-i02 own criteria、2026-10-18）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-14-i02

## Objective / own criteria

grid-auto-columns/rows-computedはfit-contentの6 callbackずつがautoへ潰れるためFAIL。既存accepted fit-content length/calcをscalarとしてparse/cascadeで保持し、computed fallback/implicit axisはfit-content(resolved length-percentage)を返す。px/%/font-relative calc・負の計算のnonnegative range・dynamic font更新・child realmを独立Chromium比較し、対象2公式IDの全callback PASSを確認する。native track sizingのfit capはp246等の別の未達であり、このPhaseのown goalはcomputed bound保持/serialization。

## Design / finite scope

既存css_declared_track.sizeにfit limitを保持、maximum_ruleへFIT_CONTENTを追加。calc引数は既存values_calcで実際に検証し、cascade_tracksはfit limitにも既存length変換/rangeを適用。computed_track_breadthはfit-content()をserializeする。新pointer/公開ABIは無く、templateのactual used queryは維持。

native solverのcoarse auto経路を今回変えない。grid_column_stretchesは既に実装済みmin/max-contentだけをnon-autoと判断し、新fit metadataの追加でlayoutを偶然変えない。layout cap/line names/animationのnative実装はこのscope外。

## Verification / acceptance boundary

最大60分。独立computed値をChromium比較、native geometryはbefore/after不変をdiagnosticとして確認（fit sizing完成とは主張しない）。固定Grid502 whole旧248 PASSとstatic Grid529旧228 PASSを維持、全callback遷移/ID/hash/rawを記録。computed own positive cases/既存DOM/CSSOMで後退0、host/target warning0・変更関数C全文/format・Acid非増加。

q11でnative animation欠落がlossy serializerによりfalse PASSを作ると確認済みなので、この選定は全callback count非減少をclearance条件にしない。全遷移を診断し、own computed contractの未達や旧whole PASS lossはunclearedにする。p248 native animationの未達やp246/q11 outcomeは維持し、Bug transferによるclearanceとはしない。全3,805 ID/p100/target関門を除外しない。

Guardrail/C全文/browser-component全文を適用。dedicated conciseは無いので全文を使う。fit-content grammar自体は既存supported length/calc範囲を保ち、new template grammar/Wayland/browser ABI/WPT/Masterは変更しない。

Event ws074-p250-q14-selected-20261018: user autonomyをcomputed fit bound最大60分へ適用。p221/p243/p246へ新scalar意味/検証とnative sizingの残りを反映。q13のrunner verified outputを使い、q12 global rawから同じGrid502/静的529のbeforeを抽出する。

Event ws074-p250-percentage-dependency-20261018: 17条件参照でzero/cancelled %を保持する依存を確認。p251をq14-i01へ追加しverified output後にi02のcomputed fitを検証する。初期scope snapshotは保存、全1491/static1561で正しいlinear typeによる影響を検証。fit coarse sizingの不変とfull native fit cap未達は維持。

Event ws074-p251-linear-type-design-20261018: [p251](../phase251/phase.md)がdirect linear calcのpercentage存在を保持する。0係数でもCSS_UNIT_PERCENTとなるためdependent computed/used pathsを全1491/static1561と独立値で検証。nonlinear min/max/clampの既存近似は未変更。p250はこのoutput後にcomputed fitを検証し、p246/q11等history/whole残りは維持。

Event ws074-p250-fit-bound-initialization-review-20261018: 未使用pointer/fieldsまで含めたvalue object初期化を確認し、一時limitをzero初期化する修正を追加。unitでguardされるため現行computed/native検証に異常は無いが、fully initialized publicationを保つ。初回globalはintermediate pairの診断として保存し、修正後pairで全1491/static1561を最初から再検証する。scope/criteria/deadlineは不変、clearance未判定。

Event ws074-q14-animation-fit-collapse-diagnosis-20261018: q14 final全件の途中で同じGrid columns/rows composition/interpolation4 IDの各4 callback、計16が旧PASS→FAIL。独立[probe](evidence/20261018-q14/animation-collapse-probe.html)でq12/final両pairのanimate/Animationがundefined、異なるfit(5px)/fit(20px)とfit(5px)/fit(40px)が旧serializerでは両auto autoとなり等しい一方、新serializerで本来の異なる値を保持することを確認。p250既定criteria通り既存native animation未実装の可視化として診断し、q11/p246の24未達やp224/p248をclearしない。全件terminal outcomeは継続待ち、runner/WPT結果判定は変更しない。

Event ws074-p250-q14-cleared-20261018: [証拠](evidence/20261018-q14/README.md)。source88713badd final pairで全harness1491 whole489→491・old PASS後退0、callback13改善/16旧animation偽陽性露出・欠落/追加/名前順変化0、全static1561 whole839維持・pixel count変化0。独立20 computed/4実寸・fit17行Chromium一致、DOM33/33/CSSOM各12/12、host/target warning0/C全文/format・Acid非増加。q14既定のown criteriaを満たしPhase/item cleared。native fit cap/nonlinear math/animationの未達をclearせず、p100/target全体/共有remote closureはpending。初版353件の停止診断は保持、terminalは初期化修正後の全件測定。

Commit receipt: q14 source88713badd、terminal evidence/history WIP `98acae7a6`をgit log/statusでread-back。history hash一致・両item clearedを確認後、q15 p190の独立runner scopeへ進む。p250 own clearance/残native fitは不変。
