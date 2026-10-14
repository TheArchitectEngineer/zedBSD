<!-- awesome-plan project=zedbsd record=ws074-p220 -->

# ws074-p220: vertical inline text geometry

Status: in-progress（q1121部分調査。whole未達）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p217 q1103設計](../phase217/evidence/20261014-q1103/README.md)、[p217 q1092縦書きhost](../phase217/evidence/20261014-q1092/README.md)。
Scoped output dependency: [p217](../phase217/phase.md)のvertical-lr文字Grid alignmentは本Phaseの縦inline line geometry出力を待つ。p217の空vertical-rl子や他水平alignmentは独立。

## 目的と受け入れ

CSS縦書き文脈でinline文字の論理inline/block軸、text-orientation、line/fragment physical geometry、glyph paint、bounds/hit/dumpを一貫させる。固定WPT007/008の単一Ahem文字がfont-size20/40/30に対応する幅を得ることを早期指標にし、一般的な文字列、wrap、混在inline、フォント、後退回帰を段階検証する。p220 wholeは合意した対象集合の実測とfull-standard conformanceを要し、初期の単一字幅だけではclearedにしない。p217 Grid alignment、p189 runner、p173全件100%は別受け入れ。

## 設計・境界

`userland/desktop/libbrowser/css/`の`text-orientation`、`layout/inline.c`/`layout.h`、paint/list、bounds/hit/dump、必要なblock/DOM geometry。論理座標をline/fragmentで表し、physical変換をlayout/paint全てに適用する。最初の有限実装は単一unwrapped sideways文字run、vertical-lr、明示height、float/atomic無しの非Grid blockを候補とし、Grid auto幅・baselineは後段で扱う。WPT本体/runner採点/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。code後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p220-addition-20261014: [p217 q1103](../phase217/evidence/20261014-q1103/README.md)で固定007/008のwidth100誤差はGrid forced stretchとinline/paintの横書き固定が重なると確認。縦文字line geometryをp217のGrid alignmentから分離し、p217の該当部分だけ本Phaseの検証済み出力へ依存させる。p189/p173分母と受け入れは不変。共有投影/GitHub publication保留。

## q1121-i01 有限部分範囲（2026-10-14 09:30 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:00 JSTまで）に適用。固定column-axis positioned-items012–016のvertical-lr/rl初回幅期待10/実60についてWPT source、最小非Grid/位置指定Grid fixture、inline line/fragment・文字方向・描画/DOM geometry経路を読み取り調査。原因と最初の有限実装scope・回帰条件を証拠化。製品browser/runner/WPT/公開ABI/`plan/master.md`/Acid変更なし。p220 whole/p217/p189/p173全件100%は本部分基準外。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)適用。

## q1121-i01 結果（2026-10-14 09:35 JST）

Event ws074-p220-q1121-partial-clear-20261014: [証拠](evidence/20261014-q1121/README.md)。固定列軸012–016の縦書き10 subtestはすべて初回Ahem文字box幅期待10/実60。非Gridの縦書き最小ページでも絶対子36×10pxで横書き行のまま。inlineの物理x/y、auto width、paint CPU/GPU、bounds/hit/dumpに縦軸表現がなく、CSS text-orientationも未実装。次は非Grid vertical-lr/単一非折返しrunを有限scopeで縦line/fragmentとsideways描画へ通し、縦Grid/absは後続。q1121部分cleared、p220/p217/p189/p173全件100%未達。browser/runner/WPT/ABI/Master/Acid無変更、共有投影/GitHub publication保留。
