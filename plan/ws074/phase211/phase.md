<!-- awesome-plan project=zedbsd record=ws074-p211 -->

# ws074-p211: CSS Grid inline-axis self alignment

Status: cleared（q1070-i01 LTR基本配置・host/WPT/Acid確認）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p190 q1062 static baseline](../phase190/evidence/20261014-q1062/README.md)、[p189 q1068 fixed-font baseline](../phase189/evidence/20261014-q1068/README.md)。

## 目的と受け入れ

CSS Grid の `justify-items` / `justify-self` が未登録で、項目の inline-axis 配置が常に左端になる一般欠陥を修正する。固定 WPT の LTR / 横書きの基本的な start/end/center/stretch を検証し、host build・CSS/レイアウト回帰・Acid3/Acid2 を維持する。RTL、縦書き、baseline、replaced 要素や auto margin 等の完全な対応は後続で検証する。p190/p189/p173 の全件受け入れは独立。

## q1070-i01 有限部分範囲

2026-10-14 04:45 JST、この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を最大90分（06:15 JSTまで）の本 Queue に適用。内部 CSS property の認識、cascade 初期値・inherit、Grid 横書き LTR の明示幅項目に対する start/end/center/stretch 配置を最小実装する。固定 manifest に属する関連 reftest と局所 fixture を修正前後で測り、旧 PASS を破らないことを確認する。build warning0、関連 host 試験、Acid3 差0・Acid2 非増加を確認。WPT/runner/公開ABI/`plan/master.md` は変更しない。90分以内に未完なら証拠と残件を保存して uncleared で止める。適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p211-addition-q1070-20261014: q1068 Grid 非 PASS と q1062 Grid pixel FAIL の共通一般欠陥候補から分離。p189/p190/p173 の分母と受け入れは維持。共有 Master/Queue/GitHub publication は Q1 競合回避で保留。

## q1070-i01 結果（2026-10-14 04:59 JST）

Event ws074-p211-q1070-clear-20261014: `justify-items` / `justify-self` の内部CSS値・cascade・computed CSSOMと、LTR横書きGridの明示幅項目の start/end/center/left/right、子要素方向による self-start/end 配置を実装。[固定WPTと検証](evidence/20261014-q1070/README.md)では代表 testharness 1 ID の subtest が旧4/18→9/18 PASS、5 FAIL→PASS/0 PASS→FAIL。関連静的reftest57件は旧15 PASS/42 FAILと全件同status、旧PASS後退0。局所配置13/13、既存物理配置21/21、CSS rule51/51、CSSOM12/12、warning0 build。Acid3 100/100・画素差0、Acid2差1865を維持。中間版のAcid回帰はalignment以外のkeywordへの誤変換を修正して消失。q1070-i01/p211 whole cleared。RTL Grid、auto幅、baseline、replaced、`place-items` 第2値等は後続範囲。p189/p190/p173全件100%は未達、共有投影/GitHub publication保留。
