<!-- awesome-plan project=zedbsd record=ws074-p215 -->

# ws074-p215: CSS Grid replaced item normal and stretch sizing

Status: cleared（q1076-i01、固定14 WPT全PASS・静的旧PASS維持）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p214 q1075](../phase214/evidence/20261014-q1075/README.md) min-content修正と [p189 q1074](../phase189/evidence/20261014-q1074/README.md) 固定Grid baseline。

## 目的と受け入れ

Gridのreplaced `<img>` で `normal` を自然寸法、明示 `stretch` をarea幅に解決し、画像の縦横比で未指定高さを決める。現在のCSS値はnormal/stretchを同一enumへ写しており、Gridのreplaced項目を幅も高さもareaへ伸ばす。内部値を区別し、非置換GridとFlexの従来default stretchを維持する。固定幅変化WPT029–032、局所fixture、静的Grid529旧PASS、Flex基本・host/Acidを検証する。p189/p190/p173全件受け入れは独立。

## q1076-i01 有限範囲

2026-10-14 05:55 JST、このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を最大90分（07:25 JSTまで）の本Queueに適用。CSS alignment内部値の`normal`分離、Grid横書きin-flow replaced imageの自然幅/明示stretch幅と縦横比高さ、Flexのnormal従来defaultを一般規則で最小修正する。固定WPT029–032、関連14 ID、局所fixture、固定静的Grid529旧PASS、固定Flex10 local pixel旧PASS、warning0 host build・CSS検証、Acid3差0/Acid2非増加を確認。WPT/runner/公開browser ABI/`plan/master.md`変更なし。上限内に未検証ならunclearedで証拠・残件を保存。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Grid L1 §6.2](https://www.w3.org/TR/css-grid-1/#grid-item-sizing)を適用。

Event ws074-p215-addition-q1076-20261014: q1075残4 WPT 029–032は100×100画像がGrid area200px幅で`normal`/`stretch`を切り替える。現在はnormalもstretchと同じ値で、Grid itemの幅200/高さ100が続く。p214の非置換fit-content出力とp190静的baselineは維持。共有Master/Queue/GitHub publication保留。

## q1076-i01 結果（2026-10-14 06:22 JST）

Event ws074-p215-q1076-clear-20261014: CSS normal/stretch内部値を分離し、Grid replaced画像の自然幅/明示stretch幅・比率高さを修正、Flex normalの従来動作を保持。[証拠](evidence/20261014-q1076/README.md)。固定幅変化系列14 WPTは旧10 PASS/4 FAIL→14/14 PASS、旧PASS後退0。局所画像7/7・Grid47/47、固定静的Grid529旧124 PASS全維持・Flex10/10、warning0 host build・CSS rule51/51・CSSOM12/12、Acid3差0・Acid2差1865。q1076-i01/p215 whole cleared。p189/p190/p173全件100%未達、共有投影/GitHub publication保留。
