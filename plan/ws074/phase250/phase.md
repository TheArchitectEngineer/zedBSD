<!-- awesome-plan project=zedbsd record=ws074-p250 -->

# ws074-p250: computed fit-content track bound

Status: in-progress（q14 computed partial）
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
