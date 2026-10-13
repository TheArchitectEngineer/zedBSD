<!-- awesome-plan project=zedbsd record=ws074-p199 -->

# ws074-p199: physical justify-content left/right

Status: cleared（2026-10-14 q1052後続証拠でwhole clearance。q1049-i01 partial outcome保持）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p198 q1048](../phase198/evidence/20261014-q1048/README.md)の縦方向参照レイアウトと固定10件baseline。p190 whole clearanceは不要。

## 目的と受け入れ

`justify-content:left/right`をCSS Box Alignmentの物理方向として保持し、横方向軸では物理左右に、column flexの上下軸ではstartに配置する。`align-*`ではleft/rightを無効として扱う。固定WPTの縦方向left/right段とhostの横方向RTL/reverse・grid回帰で検証する。該当固定testのlocal pixel PASSと関連回帰がwhole clearance条件。p197/p198の別Flexbox/BFC残差、p190 runner、p173全件100%は別。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)、[CSS Align §4.1](https://www.w3.org/TR/css-align-3/#positional-values)。libbrowser内部のみ変更し、公開ABI/Wayland境界・WPT・`plan/master.md`を変更しない。

## q1049-i01 の有限部分範囲

2026-10-14 02:15 JST、ユーザーの事前承認を適用、最大90分（03:45 JSTまで）。固定vert001a参照との差でleft/right段の子が誤って縦方向endになることを最小fixtureで確認する。CSS keyword保持とflex column fallback、rowの物理左右・RTL/reverse、既存gridのleft/right互換、align-*無効化を必要最小範囲で実装する。固定10 ID、host/CSS回帰、warning0 build、Acid3/Acid2差画素非増加を検証する。min-height/分数丸め/writing-modeの別原因、WPT/公開ABI/Master変更は含めない。問題が広がる場合は結果を記録してunclearedで停止する。

Event ws074-p199-created-20261014: q1048後のvert001a layout差はleft/right段で`right`が`CSS_ALIGN_END`へ早期同一化され、column flexの物理左右軸外fallback startを失う。CSS Alignの一般規則として[p197](../phase197/phase.md)/[p198](../phase198/phase.md)から分離。p190/p173受け入れは不変。GitHub publication保留。

## q1049-i01 結果（2026-10-14）

**部分scope cleared、whole uncleared。** CSS keywordのleft/rightを保持し、flex columnではstartへfallback、rowではwriting directionやreverseに依存しない物理左右へ配置した。align-items/content/selfではleft/rightを無効化し、gridの既存shift分岐も新enumを処理する。[固定10件](evidence/20261014-q1049/README.md)は3→5 local PASS、7→5 FAIL、timeout0。vert001aは5,600→200差画素、vert002は4,072→88、vert006は5,560→160。vert001bとwmvertには別原因が残る。局所fixture12/12、CSS rule44/44、CSSOM12/12、mutation12/12、warning0 build、Acid3 score100/100・差0/480000、Acid2差1865/120000。最初の検証ではkeyword数値の衝突により`cursor:crosshair`を誤拒否してAcid3回帰したため、検証対象をalign-*の3プロパティへ限定して再ビルド・再検証した。grid独立実行fixtureは未作成で、該当分岐のコード点検に留まる。全固定test pixel PASSに届かずwhole uncleared。Event ws074-p199-q1049-outcome-20261014: q1049-i01部分scope cleared、p199 whole uncleared。GitHub publication保留。

Event ws074-p199-later-clearance-q1052-20261014: q1049-i01のpartial cleared履歴は維持。[p200](../phase200/phase.md)分配丸め、[p201](../phase201/phase.md) min-height、[p202](../phase202/evidence/20261014-q1052/README.md) vertical-lrの後続修正で固定10件全てlocal pixel exact PASS。left/rightとgridを含む局所host21/21・CSS rule44/44・CSSOM12/12・warning0 build・Acid3差0/Acid2差1865を確認しp199 whole cleared。p190 runner/p173全件100%は別、GitHub publicationとIssue closeは保留。
