<!-- awesome-plan project=zedbsd record=ws074-p244 -->

# ws074-p244: 固定Grid行だけをまたぐitemのoverflow

Status: uncleared（q08-i01 host部分cleared、general minimum等whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-08-i01
Origin: [p243 q07の未達](../phase243/phase.md)

## Objective・criteria

grid_size_rowsがspan不足を末尾行へ無条件加算しているため、固定10pxの2行が10px/30pxになり、CSSOMで期待10px/10pxの1 callbackを失った。fixed-size rowだけをまたぐitemはtrackを伸ばさずoverflowするようにし、直接geometry/used CSSOM/固定WPTの3つで確認する。

## 最初の有限scope案

最大90分、layout/grid.cの新しいspan eligibility helperと既存span加算直前のguard。確定px/definite %の固定行のみのspanを対象にする。auto/indefinite percentage/fr等の既存track処理はこの最初の部分で全面改修しない。minmax(auto,length)の大きいautomatic minimumがmaximumを超える一般sizingは未実装部分として分離する。CSSOMを期待値へ偽装しない。

独立px/%/gap/overflowの小例をChromiumと比較し、固定10px×2の不備caseを再現→修正、auto混在/単一item/既存fixed geometryを対照にする。公式Grid testharness502とstatic Grid529を同条件比較し、q07の1 callbackを回復、既存whole/callback PASS後退0、host/target compile warning0、C全文/format、Acid3差0/Acid2非増加。実装が必要な範囲を超える場合はattemptをunclearedで止め、具体的なresume conditionを残す。

Guardrail、C全文、browser-component全文を適用。full WS conformanceはp100。公開ABI/WPT/Masterは不変。p243の既存copy queryを前提として実際のsolver値を検証し、p244 output後のp243新attemptがq07の未達を再確認する。

Event ws074-p244-created-from-q07-20261018: p243 q07のknown failureを処理するPhaseとして追加。q07 historyのunclearedは維持する。source mutationの新しいQueueを選定するまで実行しない。共有投影pending。

Event ws074-p244-q08-start-20261018: ユーザー自走指示をfixed-only span部分へ適用。q07の1 callbackを回復し、mixed/auto等を全面改修しない。static Grid529とhost/target/Acidで修正出力を検証し、その後p243 q08-i02へ渡す。

## q08-i01 結果

Event ws074-p244-q08-i01-partial-clear-20261018: [証拠](evidence/20261018-q08/README.md)。独立1→7/7 Chromium一致、DOM27/27、静的Grid225→227/529・旧PASS後退0、q07対象callback status0。host/target compile warning0、Acid3差0/Acid2差873。fixed-only host部分cleared、general minimum/auto等whole未達。verified final binaryをp243 q08-i02へ渡す。共有投影pending。
