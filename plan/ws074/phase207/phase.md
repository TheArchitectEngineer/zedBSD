<!-- awesome-plan project=zedbsd record=ws074-p207 -->

# ws074-p207: fractional textarea border rasterization

Status: cleared（2026-10-14 q1059-i01）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p206 q1058](../phase206/evidence/20261014-q1058/README.md)の縦textarea198差画素。

## 目的と受け入れ

`flexbox-basic-textarea-vert-001.xhtml`の残198差画素について、Flex分配値と参照の明示小数heightがdotted border・テキスト描画へ伝わる経路を切り分ける。一般的なdevice-pixel境界規則またはform controlの幾何欠陥を確認できれば最小修正し、当該IDのpixel exact PASS、固定Flex10/縦横textarea、host/build/Acid非増加をwhole clearance条件とする。WPT固有値への合わせ込みは禁止。原因が現runnerの比較特性・標準外の小数差であれば証拠を残し、無理な丸め変更はしない。p190/p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Flexbox 1](https://www.w3.org/TR/css-flexbox-1/)。libbrowser内部だけ変更可能、WPT/公開ABI/`plan/master.md`は変更しない。

## q1059-i01 有限部分範囲

2026-10-14 03:13 JST、このchatのユーザーによるInterop 2025全件100%までの有限Queue事前承認を適用。最大90分（04:43 JSTまで）。固定縦textarea1件のtest/referenceの寸法・paint境界・diff位置を最小fixtureと画像で測り、一般性を確認できた場合のみlibbrowser内部を小さく修正。固定3件・固定Flex10件、意味のあるhost、warning0 build、Acid3/2を測る。原因未確定/大きな描画変更が要る場合は調査証拠を記録してunclearedで停止する。全1,561件censusやWPT/runner変更は含めない。

Event ws074-p207-created-20261014: q1058で残った縦textarea1件は198差画素。差は文字と小数border分割位置に局在し、他のFlex min/max差は解消。p190/p173受け入れは不変、共有Master/Queue/GitHub publication保留。

## q1059-i01 結果（2026-10-14）

Event ws074-p207-q1059-outcome-20261014: 参照の`font` shorthandが`line-height:0`を保持していた欠陥を修正し198→110差画素。textarea border-boxの描画端だけdevice pixelへ丸め、レイアウト小数値を保ったまま固定1 IDはpixel差0へ。[証拠](evidence/20261014-q1059/README.md)。旧8回帰3件は3/3、固定Flex10/10 local pixel exact PASS。host CSS rule51/51・CSSOM12/12・submit13/13、warning0 build、Acid3差0/Acid2差1865。host-formのpaint座標2項目はこのQueueのfont修正を外しても同じ失敗を再現（既存不一致）、追加悪化なし。q1059-i01/p207 whole cleared。p206も後続証拠でcleared、p190/p173全件100%は未達、共有投影/GitHub publication保留。
