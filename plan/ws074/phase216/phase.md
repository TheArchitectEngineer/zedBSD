<!-- awesome-plan project=zedbsd record=ws074-p216 -->

# ws074-p216: Interop 2025 JavaScript module loading

Status: uncleared（q1079-i01単独module scope部分cleared、named import/HTML未実装）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1078 module evidence](../phase189/evidence/20261014-q1078/README.md) とp189固定Grid502件のq1077 baseline。

## 目的と受け入れ

`<script type="module">` のHTMLロードとJS module解析・compile・link・evaluationを一般規則として実装し、固定Gridの32 no_output IDを少なくともWPT callback付き結果へ移す。別モジュールのnamed import/export、相対URL解決、module scope、評価順、重複評価、ロード・構文失敗の報告をhost fixtureで検証する。全32 IDは固定WPT/runner条件で測る。PASSに満たないlayout欠陥は別Phaseへ渡す。後続の全JS module仕様やp189/p173全件100%は別判断。

## 設計・実装境界

現行 `page/script.c` はmodule typeをskip、`bind/window.c` はclassic `how=0`、`js/compile.c` はimport/export未対応。一度に全仕様を導入せず有限Queueに切り分ける。script tool の `--js --module` は単純式を実行できるが、top-level `var`/`function` をglobalへ漏らす。第一段階は単独moduleのscopeと厳格モードを修復する。次にnamed import/exportのbindingとURL graph、HTML module scriptのload/defer/errorへ進む。固定WPTの共通sourceは1つのnamed export functionで32 variantが同じnamed importを使う。WPT固有の文字列置換、テスト改変、PASS偽装は認めない。

適用: [Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)。対象は`userland/desktop/libbrowser/js/`、`bind/`、`page/`、`userland/desktop/browser/main.c`、`plan/ws074/tests/`のうち各Queueで指定した部分のみ。公開ABI・`plan/master.md`変更なし。全変更後にnear-final full-standard conformanceをWS074の既存受け入れPhaseで再確認する。

Event ws074-p216-addition-20261014: q1078で32 ID全件同形moduleとHTML loader・compilerの二重未対応を確認。[証拠](../phase189/evidence/20261014-q1078/README.md)。p189/p173の分母・受け入れは不変。共有投影/GitHub publication保留。

## q1079-i01 有限部分範囲（2026-10-14 06:32 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（08:02 JSTまで）に適用。JS module単独実行でtop-level var/function/class/let/constがglobalへ漏れず、module内とclosureから参照可能なbindingにする。strict top-level thisも確認する。既存`browser --js --module`とJS compiler/scopeの最小内部変更、局所host fixture、warning0 build、classic JS回帰、固定Acid3差0・Acid2非増加を検証。HTML module loader/import/export graph、WPT/runner/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。32 ID no_output解消は本部分scopeの受け入れに含めず、p216 wholeをclearedにしない。

## q1079-i01 結果（2026-10-14 06:35 JST）

Event ws074-p216-q1079-partial-clear-20261014: module top-level宣言をglobalではなくmodule bindingにし、strict top-level `this=undefined`を維持。[証拠](evidence/20261014-q1079/README.md)。局所fixture exact、classic JS14/14、build warning0、Acid3差0・Acid2差1865。q1079-i01 partial cleared、p216 wholeはimport/exportとHTML module load未実装でuncleared。固定Grid32 no_outputとp189/p173全件100%未達。共有投影/GitHub publication保留。
