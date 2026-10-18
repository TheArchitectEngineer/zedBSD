<!-- awesome-plan project=zedbsd record=ws074-p244 -->

# ws074-p244: 固定Grid行だけをまたぐitemのoverflow

Status: uncleared（q09-i01 targeted/static部分cleared、general minimum等whole未達）
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

## q08 follow-upとdesign revision

Event ws074-p244-q08-i01-invalidated-20261018: [i02証拠](../phase243/evidence/20261018-q08-i02/README.md)。minmax(auto,0px)の10 callback後退によりi01のinitial clearanceをinvalidateし、current outcomeをunclearedとした。独立7/7・静的225→227の初期証拠は保持。

Event ws074-p244-minimum-rule-redesign-20261018: plain fixed lengthとminmaxのauto/min-content/max-content/explicit length minimumをparse→cascade→layoutへ保持する。definite fixed maximaだけでなく、automatic minimumがcapacityを超えず、intrinsic minimumが未証明でないことをguard eligibilityで確認する。未証明のminimumには従来のgrowth経路を残す。一般minimumの均等分配は別の未達として保持。この変更はcss.h/internal.h/values.c/cascade.c/grid.cへ範囲が広がるため、q08を再使用せず次の有限Queueを選定する。新output後のp243は再度full502/全callbackを比較する。共有投影pending。

Event ws074-p244-q09-start-20261018: User autonomyをmetadata/guard repair最大90分へ適用。minimum ruleをNONE/length/auto/min-content/max-contentとして保持。minmax-autoでknown preferred/minimum heightとframe/marginのcontributionがcapacityを超える場合、またはratio/replaced/percentage等でproofできない場合は新guardを適用せず従来経路へ戻す。一般minimum分配を修復したとはしない。verified output後、p243新attemptで全502を再照合する。

Event ws074-p244-q09-i01-partial-clear-20261018: [証拠](evidence/20261018-q09/README.md)。minimum ruleを保持し、target10後退回復/q07元case維持、target全callback9改善/後退0、DOM27/27、static529の227 PASS維持。host/target3source warning0、Acid3差0/Acid2差873。targeted/static部分cleared、p244 whole未達。verified outputをp243 q09-i02へ渡す。
