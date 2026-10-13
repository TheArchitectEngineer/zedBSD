<!-- awesome-plan project=zedbsd record=ws074-p212 -->

# ws074-p212: CSS Grid horizontal RTL tracks and self alignment

Status: cleared（q1071-i01 RTL配置・全Grid静的旧PASS維持・Acid確認）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p211 q1070](../phase211/evidence/20261014-q1070/README.md) のLTR配置・代表WPT 9/18結果。

## 目的と受け入れ

横書き RTL Grid の明示列を右から配置し、明示幅 in-flow 項目の `justify-items` / `justify-self` の start/end/center/left/right と self-start/end をコンテナ・項目それぞれの方向に従って配置する。固定代表WPTの残り9 RTL subtestを確認し、既存9 LTR subtestと関連静的旧PASSを維持する。Gridのauto幅、abspos、縦書き、baseline、auto margin、replacedの全対応は独立。

## q1071-i01 有限部分範囲

2026-10-14 04:59 JST、このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を最大90分（06:29 JSTまで）の本 Queue に適用。`layout/grid.c` の横書きRTL明示列の物理配置と、明示幅in-flow項目のinline-axis基本配置を一般規則で最小修正する。局所LTR/RTL fixture、固定代表WPT18 subtest、p211固定静的57 IDおよびRTL Grid関連静的代表、warning0 host build、Acid3差0/Acid2非増加を確認する。WPT/runner/公開ABI/`plan/master.md`は変更しない。90分以内に成立しなければ証拠と残件を記録してunclearedで止める。適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。

Event ws074-p212-addition-q1071-20261014: p211の代表WPT18件中、残る9件はいずれもRTLコンテナで列の物理順が左からになっている。p211/p189/p190/p173の受け入れと分母は維持。共有Master/Queue/GitHub publicationはQ1競合回避で保留。

Event ws074-p212-q1071-scope-revision-20261014: RTL列配置後、固定静的旧PASS `subgrid/writing-directions-002.html` が 217差画素（最大チャネル差4）となった。auto列の余白等分で端数が残り、RTL start の shift が0.02〜0.03pxずれる一般原因を確認。ユーザーの有限Queue事前承認の範囲内で、旧PASS維持に必要なauto列端数割当をq1071の最小修正と検証へ追加し、全Grid静的529件の旧PASS監査も加える。90分上限、WPT/runner/ABI/Master不変更、p189/p190/p173受け入れは維持。最初の90件測定17 PASS/73 FAILと、端数修正後の旧PASS単独再測定PASSを履歴に残す。

## q1071-i01 結果（2026-10-14 05:09 JST）

Event ws074-p212-q1071-clear-20261014: 横書きRTL Gridの列物理順・justify-content/項目self配置を修正。auto列端数を最終列に割り当て旧PASSの0.02〜0.03px差を解消。[証拠](evidence/20261014-q1071/README.md)。固定代表WPTは9/18→18/18 PASS、whole 1 ID FAIL→PASS。関連静的90件は旧18 PASS/72 FAIL、Grid静的529件は旧123 PASS/406 FAILを全件維持、旧PASS後退0。host局所33/33・物理配置21/21・CSS rule51/51・CSSOM12/12・warning0 build、Acid3得点100/画素差0、Acid2差1865。q1071-i01/p212 whole cleared。auto幅/abspos/縦書き/本物のsubgrid等は別範囲、p189/p190/p173全件100%未達。共有投影/GitHub publication保留。
