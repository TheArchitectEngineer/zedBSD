<!-- awesome-plan project=zedbsd record=ws074-p247 -->

# ws074-p247: zero font glyph advanceとink

Status: cleared（q12-i02 own zero glyph/ink acceptance）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006
Queue: ws074-q20261018-12-i02（i01後に開始）

## Observation / goal

q11の独立例でfont-size:0の空白が約0.296875px残った。text_select_fontはCSSのzero size自体をrasterizer用の最小1pxへ上げ、advanceにもfont->size=1を使っている。p185はzero line strutだけを修正してclearedであり、その受け入れは維持する。今回の残りはglyph advance/inkとintrinsic contribution。

[CSS Fonts font-size](https://www.w3.org/TR/css-fonts-4/#font-size-prop)のnonnegative sizeと、固定WPT treeのcss/css-fonts/font-size-zero-1/2/3（特に3のzero intrinsic contribution assertion）を参照する。これら補助IDはInterop2025の固定3,805には含まれないので公式改善へ加算しない。

## First finite design candidate

text_fontのlogical size0とrasterizer用positive pixel sizeを分ける。zeroではglyph queryにzero advance/empty bitmapを返し、bold/fallbackでもphantom width/inkを作らない。positive font・fractional sizeの既存挙動と公開ABIを変更しない。text_font_metrics/char widthsはlogical sizeの計算結果を使うことを確認する。software/Vulkan paintはNULL bitmapを既存のno-ink経路へ渡す。

## Verification / bounds

実fontのnative glyph/metrics/cacheと短い独立DOM geometry/paintを前後・Chromium比較、p185 strutとpositive-font対照、DOM/CSSOM host、host/target compile warning0、C全文/format、Acid3差0/Acid2非増加。font変更は全領域へ影響するため、固定testharness既存適格1,455 IDとstatic reftest1,561 IDを全測定し、旧whole/callback PASSの照合・未対応328/413・crash48を保持する案。無限調査をせず最大90分のQueueへ選定してからsource生成する。

Guardrail/C全文/browser-component全文を適用。user-visible font minimum preferenceを新設しない。負font入力/positive subpixel rasterizerの全面変更、full text shaping/ligatures/decorationはこの最初の範囲外。全WS conformance/production link/runtimeはp100/後段。共有projection pending。

Event ws074-p247-planned-from-q11-20261018: bug index/Phaseを検索し、p185 strutの既知結果と別のadvance残りを確認。q11のsnapshot/acceptanceは変更せず後続候補として追加。

Event ws074-p247-q12-selected-20261018: 最大90分のzero glyph scopeを選定、i01 verified retention後に実装する。global font changeなのでfixed eligible1455/static1561をq11 frozen pairと新pairで比較し、既知q11 animation24 lossをfont修正へ誤帰属しない。未対応分とwholeの全3,805/p100関門は維持。

Event ws074-p247-q12-i02-start-20261018: i01のactual retained raw proofを確認しzero glyph実装を開始。beforeはfa4ac8002のq11 fixed pair、24既知animation lossを含む現状から直接比較する。

Event ws074-p247-q12-native-checkpoint-20261018: native zero56/56・既存text20/20、独立DOM14/14 Chromium一致、DOM30/30、CSSOM/mutation各12/12、p185 strut PASS。zero glyph paintは黒15pixel→0、positive paintは305pixel維持/画素差0。host/target compile warning0、変更関数自動規約候補0、Acid3差0/Acid2差873。全eligible前後比較は継続中なのでclearしない。補助font-size-zero 3件は0→2 PASS、残るzero-1は次段落が16px上（test y51.20/ref67.20）になるmargin-collapseの既存残りとして別に診断し、zero inkの修復と混同しない。native fixture/DOM fixtureはactive p247/p100から参照し、再利用のTools/AAT登録・移設はQ1のshared投影へpending。

Event ws074-p247-q12-cleared-20261018: [証拠](evidence/20261018-q12/README.md)。native56/56/text20/20/独立14/14・DOM30/30、zero ink15→0/positive差0、harness1455 whole489/callback11534とstatic1561 whole839を前後維持、全old PASS後退0/欠落0。Acid3差0/Acid2差873、host/target warning0。own zero glyph/ink acceptanceを満たしcleared。補助zero1はp249のmargin残り、WS全体/target/p100等未達。GitHub publish/close/共有投影pending。
