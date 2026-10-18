# q14 / linear percentage type・computed fit bound cleared

2026-10-18 13:41 JST。source `88713badd` WIP（初版 `da886cd71`、fit-bound valueの全field初期化を追加して全件を再測定）。p251 direct linear calc typeとp250 computed fit-content boundのown criteriaを満たした。Queue両item cleared、WS074全体は未達。

## Changes and independent verification

- direct percentage termの存在をprivate scalar flagで保持し、加減算・scalar積で係数0になってもlinear calcの型を維持。20 computed値と4 flex実寸がChromiumと一致。nested nonlinear mathの既存近似は未変更。
- fit-contentのaccepted length/calcを実parseし、boundをcascade・computed serializerへ渡す。font単位/負calc range/0%/dynamic font/child realm/invalid argumentの17行でChromium一致。native fit capは未実装、coarse auto solver/actual used-track queryを維持。
- DOM33/33、native CSSOM/mutation各12/12、host full build warning0・変更C4file exact target compile warning0。生成7関数をC全文/manual/format19で確認し、対象style候補0、full file既存候補146/18/32/134の追加0。最終source/pair hash、実commandとversionを保存。
- 最終pairでAcid3 score/inventory100・RGB差0、Acid2差873維持。public browser ABI/Wayland境界/WPT/runner結果判定は不変。

## Fixed Interop verification

全eligible testharness **1,491 ID**を実行し、progress/report/raw JSON/gzip bytes・ID集合を全照合。hash `8eb1a5cacf64d34ab0b36d026b6f1335725ce9f9f886ba69b9c286bac8319727`。beforeは同じq12 frozen pairの1455件とq13 variant36件の合成で、全IDの既存versioned rawとdecoded SHA256を[baseline](baseline-1491.json)で照合した。

- whole **489→491 PASS**、FAIL952→950、harness_error3/no_completion21/no_output24/multiple_completions2は同じ。old whole PASS後退0。
- callback **13改善/16後退**。PASS11534→11531、FAIL20223→20226、NOTRUN14。欠落/追加/名前順変化0。
- 改善はflex-basisの0%型1とgrid-auto-columns/rows-computed各6。後者2 IDは全callback PASSへ移行。
- 16後退はGrid columns/rows composition/interpolation各4。beforeでは異なるfit-content値が両方auto autoへ潰れて等しく、新serializerでは区別されることを[独立probe](animation-collapse-probe.html)とbefore/after出力で直接確認。両pairでanimate/Animation undefined、実animation未対応の可視化。q14既定criteriaに従い全遷移を記録し、p224/p248の実装やq11/p246の未達を解消したとはしない。callback全体の非減少を主張しない。
- 固定Grid502は248→250 whole PASS、callback6471→6467 PASS。固定全static **1,561 ID**は839 PASS/722 FAIL・error/timeout0を維持、old PASS後退0・全reference pixel count変化0。static Grid529の228 PASSも維持。

最終Cを含むpairをfreezeした後は上書きしていない。最初のglobal353件は一時valueの全field初期化reviewにより停止した診断で、全raw/scratchをbuildに保持し、terminal比較に使っていない。修正後1491/1561全件がterminal evidence。process20秒/settle5000ms、coordinator最大2400秒、q14の14:09 deadline内に終了。

## Remaining work / synchronization

固定全3805の確認済みwhole PASSは **1330（34.954%）**。q03/q06 censusからharness6/static3改善・old PASS後退0。未対応harness292/static413、crash48の正式wait/load/paint確認は分母に保持。native fit sizing/line names/child・vertical used copy、native animation等は未達。flex-basis grammar/range/content baseはp252候補。

次はcrash48の正式待機条件とraw retentionを既存p190で有限選定する候補を優先し、その後p252。p100全WS source規約・target全link/runtime/別OS等、Tools/AAT登録、Master/共有Queue/GitHub publish/closeはpending。push無し。元JSONとgzipはbuildに両方保持し、repoにはgzip rawを保存。cleanupはQ1所有。
