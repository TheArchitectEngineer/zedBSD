<!-- awesome-plan project=zedbsd record=ws074-p247 -->

# ws074-p247: zero font glyph advanceとink

Status: planned
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: 未選定（q11後の候補）

## Observation / goal

q11の独立例でfont-size:0の空白が約0.296875px残った。text_select_fontはCSSのzero size自体をrasterizer用の最小1pxへ上げ、advanceにもfont->size=1を使っている。p185はzero line strutだけを修正してclearedであり、その受け入れは維持する。今回の残りはglyph advance/inkとintrinsic contribution。

[CSS Fonts font-size](https://www.w3.org/TR/css-fonts-4/#font-size-prop)のnonnegative sizeと、固定WPT treeのcss/css-fonts/font-size-zero-1/2/3（特に3のzero intrinsic contribution assertion）を参照する。これら補助IDはInterop2025の固定3,805には含まれないので公式改善へ加算しない。

## First finite design candidate

text_fontのlogical size0とrasterizer用positive pixel sizeを分ける。zeroではglyph queryにzero advance/empty bitmapを返し、bold/fallbackでもphantom width/inkを作らない。positive font・fractional sizeの既存挙動と公開ABIを変更しない。text_font_metrics/char widthsはlogical sizeの計算結果を使うことを確認する。software/Vulkan paintはNULL bitmapを既存のno-ink経路へ渡す。

## Verification / bounds

実fontのnative glyph/metrics/cacheと短い独立DOM geometry/paintを前後・Chromium比較、p185 strutとpositive-font対照、DOM/CSSOM host、host/target compile warning0、C全文/format、Acid3差0/Acid2非増加。font変更は全領域へ影響するため、固定testharness既存適格1,455 IDとstatic reftest1,561 IDを全測定し、旧whole/callback PASSの照合・未対応328/413・crash48を保持する案。無限調査をせず最大90分のQueueへ選定してからsource生成する。

Guardrail/C全文/browser-component全文を適用。user-visible font minimum preferenceを新設しない。負font入力/positive subpixel rasterizerの全面変更、full text shaping/ligatures/decorationはこの最初の範囲外。全WS conformance/production link/runtimeはp100/後段。共有projection pending。

Event ws074-p247-planned-from-q11-20261018: bug index/Phaseを検索し、p185 strutの既知結果と別のadvance残りを確認。q11のsnapshot/acceptanceは変更せず後続候補として追加。
