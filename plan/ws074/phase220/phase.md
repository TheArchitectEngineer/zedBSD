<!-- awesome-plan project=zedbsd record=ws074-p220 -->

# ws074-p220: vertical inline text geometry

Status: in-progress（2026-10-18 Q1: Status の行が無かったので写した。元: in-progress / q1121–q1122部分cleared）

## q1184-i01 結果（2026-10-14 13:44 JST）

Event ws074-p220-q1184-partial-clear-20261014: [証拠](evidence/20261014-q1184/README.md)。水平RTL固定Grid直接vertical-rl/RTL absolute子のrow2/col2左上insetに2番目row trackの縦inline roomとstatic areaを適用。独立最小矩形は5,10,36,40→5,160,40,36でChromium一致。固定non-static007水平LTR/RTL両callback whole PASS、縦2 callback FAILでfile whole未達。旧WPT、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865維持。q1184有限部分範囲cleared、p220/p217/p189/p173 whole未達、共有投影/GitHub publication保留。

## q1184-i01 有限部分範囲（2026-10-14 13:43 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（15:13 JSTまで）に適用。positioned horizontal-tb RTL固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute vertical-rl/RTL auto-size ASCII子、row2/col2明示span1・left/top明示で確定2番目row trackのinline room/static areaを使う。独立最小ページChromium矩形一致、固定non-static007水平RTL callback whole PASS、旧水平LTR callback/非static001/縦positioned代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/parent frame/auto line、WPT/runner/公開ABI/`plan/master.md`変更なし。WPT全file wholeは基準外。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1183-i01 結果（2026-10-14 13:42 JST）

Event ws074-p220-q1183-partial-clear-20261014: [証拠](evidence/20261014-q1183/README.md)。水平RTL固定Grid直接vertical-lr/RTL・vertical-rl/RTL absolute子のrow1明示左右insetに確定row trackの縦inline roomとstatic area/Y alignmentを適用。独立2子矩形は5,0,36,10|198,0,42,30→155,0,10,36|110,0,30,42でChromium一致。固定non-static007水平RTL callbackは先頭2子を全通過、4番目vertical-rl/RTL子width40→60でFAILする。旧WPT、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865維持。q1183有限部分範囲cleared、p220/p217/p189/p173 whole未達、共有投影/GitHub publication保留。

## q1183-i01 有限部分範囲（2026-10-14 13:39 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（15:09 JSTまで）に適用。positioned horizontal-tb RTL固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute vertical-lr/RTL row1/col1 left明示とvertical-rl/RTL row1/col2 right明示のauto-size ASCII子で、確定row track inline roomを使う。独立2子最小ページChromium矩形一致、固定non-static007水平RTL callbackの最初2子全検査通過、旧水平LTR callback/非static001/縦positioned代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/parent frame/auto line、WPT/runner/公開ABI/`plan/master.md`変更なし。WPT全file wholeは基準外。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1182-i01 結果（2026-10-14 13:38 JST）

Event ws074-p220-q1182-partial-clear-20261014: [証拠](evidence/20261014-q1182/README.md)。水平固定Grid直接vertical-lr/RTL absolute ASCII子のrow2/col2左上insetに確定row trackの縦inline roomを適用。独立最小子矩形は105,160,36,40→105,160,40,36でChromium一致。固定non-static007水平LTR callbackは4子全PASS、他3 callback FAILでwhole file未達。旧WPT、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865維持。q1182有限部分範囲cleared、p220/p217/p189/p173 whole未達、共有投影/GitHub publication保留。

## q1182-i01 有限部分範囲（2026-10-14 13:37 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（15:07 JSTまで）に適用。positioned horizontal-tb LTR固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute vertical-lr/RTL auto-size ASCII子、両axis明示span1でrow2/col2・left/top明示。確定row trackのinline roomとRTL縦進行を最小修正する。独立最小ページChromium矩形一致、固定non-static007水平LTR callback whole PASS、旧non-static001/vertical positioned代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/RTL親/parent frame/auto line、WPT/runner/公開ABI/`plan/master.md`変更なし。WPT全file wholeは基準外。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1181-i01 結果（2026-10-14 13:36 JST）

Event ws074-p220-q1181-partial-clear-20261014: [証拠](evidence/20261014-q1181/README.md)。水平固定Grid直接vertical-rl/LTR absolute ASCII子に確定row trackの縦inline roomと`align-self:end`のY軸を適用。独立最小子矩形は198,0,42,30→210,0,30,42でChromium一致。固定non-static007水平LTR callbackは2番目子width30/height70/offsetを通過し、4番目vertical-lr/RTL子width40→60でFAILする。旧WPT、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865維持。q1181有限部分範囲cleared、WPT wholeとp220/p217/p189/p173 whole未達、共有投影/GitHub publication保留。

## q1181-i01 有限部分範囲（2026-10-14 13:33 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（15:03 JSTまで）に適用。positioned horizontal-tb LTR固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute vertical-rl/LTR auto-size ASCII子、両axis明示span1・right明示で、縦inline roomを確定row trackから取る。独立最小ページChromiumの子矩形一致、固定non-static007水平LTRの2番目子width30/height70/offsetを測り、旧先頭子/非static001/縦positioned代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/RTL/parent frame/auto line、WPT/runner/公開ABI/`plan/master.md`変更なし。WPT whole PASSは基準外。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1180-i01 結果（2026-10-14 13:31 JST）

Event ws074-p220-q1180-partial-clear-20261014: [証拠](evidence/20261014-q1180/README.md)。水平固定Grid直接vertical-lr/LTR absolute ASCII子に確定row trackの縦inline roomを適用。独立最小子矩形は5,0,36,10→5,0,10,36でChromium一致。固定non-static007水平LTR callbackは最初子のwidth10/height60/offsetを通過し、次のvertical-rl子width30→70でFAILする。旧WPT、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865維持。q1180有限部分範囲cleared、WPT wholeとp220/p217/p189/p173 whole未達、共有投影/GitHub publication保留。

## q1180-i01 有限部分範囲（2026-10-14 13:27 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（14:57 JSTまで）に適用。positioned horizontal-tb LTR固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute vertical-lr/LTR auto-size ASCII子、両axis明示span1・left明示で、縦inline roomを確定row trackから取る。独立最小ページChromiumの子矩形一致、固定non-static007水平LTR先頭子のwidth10/height60とoffsetを測り、旧水平/縦positioned代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/RTL/parent frame/auto line、WPT/runner/公開ABI/`plan/master.md`変更なし。WPT whole PASSは基準外。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1169-i01 有限部分範囲（2026-10-14 12:47 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（14:17 JSTまで）に適用。positioned LTR固定2×2 vertical-lr/rl inline-grid、明示px track/gap0の直接absolute auto-size ASCII子で、子が同writing-mode・direction:rtlの場合に縦inline寸法経路へ通す。固定row-axis010/011の縦初回width10/height期待値、旧001/008/009とcolumn010/011初回、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。文字描画の全面一致/static area/auto lines/他Grid、WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1169-i01 結果（2026-10-14 12:48 JST）

Event ws074-p220-q1169-partial-clear-20261014: [証拠](evidence/20261014-q1169/README.md)。同writing-mode固定縦Grid absolute子のRTLを限定縦inline寸法経路へ通し、固定row-axis010/011の縦初回width60→10、後続offsetTop差へ進んだ。旧6件PASSとcolumn010/011幅改善維持、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。q1169部分cleared、文字字形順序とstatic area、p220/p217/p189/p173 whole未達。共有投影/GitHub publication保留。

## q1168-i01 有限部分範囲（2026-10-14 12:42 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（14:12 JSTまで）に適用。positioned LTR固定2×2 vertical-lr/rl inline-grid、明示px track/gap0の直接absolute auto-size ASCII子に限り、子が親と逆のvertical writing-modeでも縦inline geometryへ通す。固定column/row-axis010/011の縦初回width10/高さ期待一致、旧001–009代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。static area/逆direction/auto lines/他Grid、WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p220/p217/p189/p173 whole基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1168-i01 結果（2026-10-14 12:45 JST）

Event ws074-p220-q1168-uncleared-20261014: [証拠](evidence/20261014-q1168/README.md)。column-axis010/011は逆writing-mode子で縦初回width60→10、static offset差へ進んだ。row-axis010/011は逆writing-modeではなく逆direction子であり、q1168の明示scope外のためwidth60が残る。承認criteriaを全て満たせずq1168-i01はuncleared。column側の検証済み改善は保持。旧6件PASS、host CSS51/JS14/Grid48、warning0 build、Acid3差0/Acid2差1865。次は別QueueでRTL縦文字寸法、後続にstatic area。p220/p217/p189/p173 whole未達。共有投影/GitHub publication保留。

## q1148-i01 有限部分範囲（2026-10-14 11:28 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（12:58 JSTまで）に適用。q1144の非折返しvertical-lr/LTR text-only行のLatin sideways字形を、縦の物理line/fragment座標と時計回り90度のbitmap配置でCPU/GPU描画。shadow/underlineは本scopeの対象外。非対称字形の画素参照・CPU/GPU一致、shader検証、warning0 build、旧縦/横WPT PASS、host/Acid3差0・Acid2非増加を検証。別writing-mode/複雑inline/折返し、WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p220/p225/p189/p173 wholeは部分受け入れに含めない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1148-i01 結果（2026-10-14 11:34 JST）

Event ws074-p220-q1148-partial-clear-20261014: [証拠](evidence/20261014-q1148/README.md)。Latin F字形の縦描画は横字形の時計回り90度回転と画素完全一致、CPU/GPUは通常・clip含め差0/16,384。upright/非ASCIIは既存経路へ戻る。shader生成・SPIR-V検証、warning0 build、固定縦Grid8件+旧縦横2件whole PASS、host CSS51/JS14/Grid48、Acid3 score100/inventory100/差0、Acid2差1865を確認。q1148部分cleared、p220 wholeは他orientation/折返し等が残りin-progress、p225/p189/p173 whole未達。共有投影/GitHub publication保留。

## q1162-i01 有限部分範囲（2026-10-14 12:24 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（13:54 JSTまで）に適用。positioned vertical-lr/rl LTR inline-gridの直接absolute子、両Grid軸明示固定px track/gap0、auto物理width/height、ASCII textと明示brだけ、非replaced/非float/非marker/非uprightに限定し、既存縦inline geometry/sideways paintを適用。固定positioned列軸/行軸001と代表017の縦subtest初回width10/height60の修正を確認し、whole残差はstatic areaと分離。非Grid/旧vertical Grid/水平WPT、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を検証。Grid static area/WPT/runner/公開ABI/`plan/master.md`変更なし。未達ならuncleared。p220/p217/p189/p173 wholeは基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1162-i01 結果（2026-10-14 12:28 JST）

Event ws074-p220-q1162-partial-clear-20261014: [証拠](evidence/20261014-q1162/README.md)。固定px trackのpositioned縦Grid直接absolute ASCII文字子を既存縦inline/shrink-to-fit/sideways paintへ通す。最小lr/rl両子10×36px・hit/paint・CPU/GPU一致。固定positioned列軸001/行軸001・017の縦初回幅10/実60と最終高さ30の差は解消し、次はoffsetTop/offsetLeft差。3 wholeはFAILのまま。旧縦Grid32+縦横2 PASS、warning0 build、host CSS51/JS14/Grid48、Acid3差0/Acid2差1865。q1162部分cleared、p220 wholeとp217 static area/p189/p173全件100%未達。共有投影/GitHub publication保留。

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
