<!-- awesome-plan project=zedbsd record=ws074-p220 -->

# ws074-p220: vertical inline text geometry

## q1148-i01 有限部分範囲（2026-10-14 11:28 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:58 JSTまで）に適用。q1144の非折返しvertical-lr/LTR text-only行のLatin sideways字形を、縦の物理line/fragment座標と時計回り90度のbitmap配置でCPU/GPU描画。shadow/underlineは本scopeの対象外。非対称字形の画素参照・CPU/GPU一致、shader検証、warning0 build、旧縦/横WPT PASS、host/Acid3差0・Acid2非増加を検証。別writing-mode/複雑inline/折返し、WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p220/p225/p189/p173 wholeは部分受け入れに含めない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1148-i01 結果（2026-10-14 11:34 JST）

Event ws074-p220-q1148-partial-clear-20261014: [証拠](evidence/20261014-q1148/README.md)。Latin F字形の縦描画は横字形の時計回り90度回転と画素完全一致、CPU/GPUは通常・clip含め差0/16,384。upright/非ASCIIは既存経路へ戻る。shader生成・SPIR-V検証、warning0 build、固定縦Grid8件+旧縦横2件whole PASS、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865を確認。q1148部分cleared、p220 wholeは他orientation/折返し等が残りin-progress、p225/p189/p173 whole未達。共有投影/GitHub publication保留。

## q1157-i01 有限部分範囲（2026-10-14 12:10 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:40 JSTまで）に適用。非Grid、明示px content width/height、vertical-rl/LTR、text-align start/left、ASCII sideways、単一非折返しrunと明示br、float/atomic/replaced/marker無しに限定。既存vertical-lr inline幾何/CPU-GPU縦paintを右端からの行進行へ拡張し、q1156最小ページの矩形/画像・hit/dump、旧縦左/横書きWPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。Grid本体/WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p220/p225/p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1157-i01 結果（2026-10-14 12:13 JST）

Event ws074-p220-q1157-partial-clear-20261014: [証拠](evidence/20261014-q1157/README.md)。非Grid・確定px content width/heightのASCII vertical-rl/LTRを既存縦inline幾何/時計回り縦paintへ通し、行を右から左へ配置。単一/2行DOM矩形・hit/dump、非対称F画素の縦左版から8px移動した完全一致とCPU/GPU一致を確認。旧縦左Grid16+縦横2 whole PASS、縦右Grid対照FAIL不変、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。q1157部分cleared、p220 wholeは他writing-mode/折返し等、p225はGrid物理行反転、p189/p173全件100%未達。共有投影/GitHub publication保留。

## q1156-i01 有限部分範囲（2026-10-14 12:08 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:38 JSTまで）に適用。q1144非Grid単一行/明示改行vertical-lr最小ページをvertical-rlへ変え、Chromium/browserのDOM矩形、layout/paint dump、CPU/GPU画像を比較。子の物理右始まり/行進行とpaint方向を切り分け、p220実装とp225 Grid依存の有限境界を記録。読取と証拠ページのみ、browser/runner/WPT/ABI/`plan/master.md`/Acid変更なし。p220/p225/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1156-i01 結果（2026-10-14 12:09 JST）

Event ws074-p220-q1156-partial-clear-20261014: [非Grid vertical-rl診断](evidence/20261014-q1156/README.md)。単一/2行ASCIIのChromium矩形は右始まり縦進行、browserは左始まり横進行。layout/paint dumpでも横向き、CPU/GPUはbyte同一。inlineのbounded縦経路がvertical-lr専用であることを確認。次はp220の単純vertical-rl文字geometry/paintを有限実装し、p225固定/auto Grid右向けは別範囲。q1156診断部分cleared、p220/p225/p189/p173 whole未達。共有投影/GitHub publication保留。

## q1144-i01 有限部分範囲（2026-10-14 11:03 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:33 JSTまで）に適用。vertical-lr/LTRの非Grid text-only block内、単一非折返し文字行と明示brの複数行だけについて、inline advance/line block幅を物理height/widthへ写し、auto高さ・intrinsic幅・DOM bounds/hit/dumpを整合。float/atomic/replaced/marker、折返し、vertical-rl/RTL、縦paintは範囲外で既存経路へ戻す。10px最小fixtureの単一行物理幅10/高さ36と複数行、旧horizontal/vertical whole PASS、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加を確認。固定vertical-lr Grid WPT2件は診断測定するがPASSはこの非Grid部分基準外。WPT/runner/公開ABI/`plan/master.md`変更なし。p220/p225/p217/p189/p173 wholeは未達を保つ。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

Status: in-progress（q1144限定縦line geometry確認。paint/whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p217 q1103設計](../phase217/evidence/20261014-q1103/README.md)、[p217 q1092縦書きhost](../phase217/evidence/20261014-q1092/README.md)。
Scoped output dependency: [p217](../phase217/phase.md)のvertical-lr文字Grid alignmentは本Phaseの縦inline line geometry出力を待つ。p217の空vertical-rl子や他水平alignmentは独立。

## q1144-i01 結果（2026-10-14 11:11 JST）

Event ws074-p220-q1144-partial-clear-20261014: [証拠](evidence/20261014-q1144/README.md)。限定vertical-lr/LTRのtext-only inline行を物理座標へ写し、10px最小ページの単一行spanを36×13→10×36、明示brの2行を36×23→20×36に修正。hit/dumpも縦位置と一致。物理height20で36px文字の範囲外例は従来経路へ戻る。固定Grid stretch001/002は両whole FAIL→PASS（Grid itemのauto高さ・intrinsic幅も改善）。旧whole PASS2、CSS51/JS14/Grid48、warning0 build、Acid3 score100/inventory100/画素差0、Acid2差1865維持。paint dumpは字形をなお横方向に配列し、縦paint CPU/GPU・他writing-mode/折返しは未実装。q1144部分cleared、p220/p225/p217/p189/p173 whole未達。共有投影/GitHub publication保留。

## 目的と受け入れ

CSS縦書き文脈でinline文字の論理inline/block軸、text-orientation、line/fragment physical geometry、glyph paint、bounds/hit/dumpを一貫させる。固定WPT007/008の単一Ahem文字がfont-size20/40/30に対応する幅を得ることを早期指標にし、一般的な文字列、wrap、混在inline、フォント、後退回帰を段階検証する。p220 wholeは合意した対象集合の実測とfull-standard conformanceを要し、初期の単一字幅だけではclearedにしない。p217 Grid alignment、p189 runner、p173全件100%は別受け入れ。

## 設計・境界

`userland/desktop/libbrowser/css/`の`text-orientation`、`layout/inline.c`/`layout.h`、paint/list、bounds/hit/dump、必要なblock/DOM geometry。論理座標をline/fragmentで表し、physical変換をlayout/paint全てに適用する。最初の有限実装は単一unwrapped sideways文字run、vertical-lr、明示height、float/atomic無しの非Grid blockを候補とし、Grid auto幅・baselineは後段で扱う。WPT本体/runner採点/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。code後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p220-addition-20261014: [p217 q1103](../phase217/evidence/20261014-q1103/README.md)で固定007/008のwidth100誤差はGrid forced stretchとinline/paintの横書き固定が重なると確認。縦文字line geometryをp217のGrid alignmentから分離し、p217の該当部分だけ本Phaseの検証済み出力へ依存させる。p189/p173分母と受け入れは不変。共有投影/GitHub publication保留。

## q1121-i01 有限部分範囲（2026-10-14 09:29 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（10:59 JSTまで）に適用。固定column-axis positioned-items012–016のvertical-lr/rl初回幅期待10/実60についてWPT source、最小非Grid/位置指定Grid fixture、inline line/fragment・文字方向・描画/DOM geometry経路を読み取り調査。原因と最初の有限実装scope・回帰条件を証拠化。製品browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p220 whole/p217/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1121-i01 結果（2026-10-14 09:31 JST）

Event ws074-p220-q1121-partial-clear-20261014: [証拠](evidence/20261014-q1121/README.md)。固定列軸012–016の縦書き10 subtestはすべて初回Ahem文字box幅期待10/実60。非Gridの縦書き最小ページでも絶対子36×10pxで横書き行のまま。inlineの物理x/y、auto width、paint CPU/GPU、bounds/hit/dumpに縦軸表現がなく、CSS text-orientationも未実装。次は非Grid vertical-lr/単一非折返しrunを有限scopeで縦line/fragmentとsideways描画へ通し、縦Grid/absは後続。q1121部分cleared、p220/p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。

## q1122-i01 有限部分範囲（2026-10-14 09:32 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:02 JSTまで）に適用。CSS `text-orientation:mixed/upright/sideways`の値解析・初期値mixed・継承/initial/unset・内部computed stateとCSSOM返却、関連`writing-mode` CSSOM返却を追加。無効値拒否、親子/上書き/未設定とcamel-case/getPropertyValue最小host検証、CSS/JS/Grid、warning0 build、旧WPT PASS、Acid3差0/Acid2非増加。縦レイアウト・描画、WPT/runner/公開ABI/`plan/master.md`変更なし。p220 whole/p217/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1122-i01 結果（2026-10-14 09:34 JST）

Event ws074-p220-q1122-partial-clear-20261014: [証拠](evidence/20261014-q1122/README.md)。CSS `text-orientation`の3値、初期mixed/継承・CSS-wide値、無効値拒否、DOM style変更、CSSOMと関連`writing-mode`返却を追加。最小host PASS、CSS51/JS14/Grid48、warning0 build、旧WPT whole PASS2件、Acid3 score100/inventory100/画素差0、Acid2差1865。縦line/paintは未実装で固定縦Grid WPTは未達。q1122部分cleared、p220/p217/p189/p173全件100%未達。共有投影/GitHub publication保留。
