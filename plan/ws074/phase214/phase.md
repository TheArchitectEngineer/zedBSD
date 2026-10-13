<!-- awesome-plan project=zedbsd record=ws074-p214 -->

# ws074-p214: CSS Grid fit-content min-content floor

Status: cleared（q1075-i01、fit-content下限と旧PASS維持）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p213 q1073](../phase213/evidence/20261014-q1073/README.md) auto幅と [p189 q1074](../phase189/evidence/20261014-q1074/README.md) 固定Grid testharness baseline。

## 目的と受け入れ

p213の非stretch Grid auto幅が area 幅で max-content を単純に切り、改行不能な最長内容より小さくなる欠陥を修正する。既存 `layout_shrink_to_fit` が使う min-content 測定を内部 Grid に共有し、fit-content幅を min-content 以上にする。固定代表WPTの before/after 幅、局所fixture、固定静的Grid529旧PASS、host/Acidを検証する。縦書き、replaced、明示 min/max は別範囲。p189/p190/p173全件受け入れは独立。

## q1075-i01 有限範囲

2026-10-14 05:46 JST、このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を最大90分（07:16 JSTまで）の本Queueに適用。`layout/position.c` の既存 min-content 測定を内部layout APIへ移し、`layout/grid.c` の横書き・in-flow・非置換auto幅非stretchに min-content 下限を設ける。代表 `/css/css-grid/alignment/grid-alignment-implies-size-change-025.html` と関連系列、局所fixture、固定静的Grid529旧PASS、warning0 host build、Acid3差0/Acid2非増加を確認。WPT/runner/公開browser ABI/`plan/master.md`変更なし。上限内に未検証ならunclearedで証拠・残件を保存。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。[CSS Sizing 3](https://www.w3.org/TR/css-sizing-3/)のfit-content定義と既存 `layout_shrink_to_fit` を参照。

Event ws074-p214-addition-q1075-20261014: q1074の代表失敗 `grid-alignment-implies-size-change-025` は改行不能「XXXXXX」幅120pxがarea80pxへ切られ、期待before120/after80のbeforeが80になる。p213後続のfit-content下限を分離。p189/p190/p173分母・受け入れは維持、共有Master/Queue/GitHub publication保留。

## q1075-i01 結果（2026-10-14 05:54 JST）

Event ws074-p214-q1075-clear-20261014: 既存min-content測定を内部layout APIとしてGridでも使い、非stretch auto幅のfit-content下限を追加。[証拠](evidence/20261014-q1075/README.md)。固定幅変化系列14 IDは旧6 PASS/8 FAIL→10 PASS/4 FAIL、4 whole FAIL→PASS、旧PASS後退0。固定静的Grid529は旧124 PASS/405 FAILを全維持、局所47/47、host build warning0・CSS rule51/51・CSSOM12/12、Acid3差0・Acid2差1865。q1075-i01/p214 whole cleared。残る幅変化4件はreplaced image固有、p189/p190/p173全件100%未達。共有投影/GitHub publication保留。
