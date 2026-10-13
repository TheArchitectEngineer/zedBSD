<!-- awesome-plan project=zedbsd record=ws074-p220 -->

# ws074-p220: vertical inline text geometry

Status: planned（q1103設計。実装Queue未選定）
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
