# q09-i01 minimum ruleとfixed-span eligibility / partial cleared

2026-10-18。minimum rule enum（NONE/length/auto/min-content/max-content）をdeclared trackとcomputed trackへ保持し、parse→cascade→layoutへ渡す。既存のgrammar/serializer/coarse track kindは維持。repeat等の既存struct copyが同じmetadataを保持する。

Guardは確定maximumに加えてlower-bound ruleを検査する。intrinsic/percentage minimumや、ratio/replaced/未知のpreferred/minimum heightを含む場合は従来経路へ渡す。minmax-autoのsupported caseではknown preferred/minimum content heightとmargin/border/paddingのunavoidable contributionを64-bitで計算し、track/gap capacityを超えない場合だけoverflow guardを適用する。plain fixed trackにはautomatic minimumを発生させない。一般minimum分配は未実装のまま。

q08の10後退は対象rawで全件PASSへ回復、q07元の1 callbackもPASSを維持。対象2 IDの全callback名/順序をq07と照合し9改善/後退0。独立7条件を含むDOM27/27、CSSOM12/12、mutation12/12。unproven controls（auto/indef-percent/minmax-large）はq07の従来出力へ戻し、Chromiumとの差を修復したとはしない。

静的Grid529 unique IDは227 PASS/302 FAIL、error/timeout0を維持し旧227 PASS後退0。manifest/全ID/checkpoint/report照合。host build warning0、production amd64 values/cascade/gridの3 source compile warning0、C全文のnew code/変更paragraph review・clang-format19.1.7・diff check。style candidate309→309で新規候補0。Acid3 score100/画素差0、Acid2差873。

q09-i01のtargeted/static部分cleared、p244 wholeはgeneral minimum分配等が残りuncleared。q09-i02が全502/全callbackを比較するまで広い回復を確定しない。q07/q08 uncleared historyは保持。public browser ABI/WPT/Master不変、production link/target runtime未実施。共有投影pending。
