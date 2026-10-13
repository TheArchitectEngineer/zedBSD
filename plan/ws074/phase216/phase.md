<!-- awesome-plan project=zedbsd record=ws074-p216 -->

# ws074-p216: Interop 2025 JavaScript module loading

Status: uncleared（q1079–q1081 partial cleared、module graph/HTML未実装）
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

## q1080-i01 有限部分範囲（2026-10-14 06:36 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（08:06 JSTまで）に適用。JS module内の`export`付きfunction/class/var/let/const宣言を通常宣言と同じscope/hoist/実行へ接続し、module内から参照可能でglobalへ漏れないことを確認する。`export {}`、default、re-export、import/module graph/HTML loaderは含めず未対応を明示維持。局所fixture、classic JS14件、warning0 host build、固定Acid3差0・Acid2非増加を検証する。WPT/runner/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。module namespaceと固定Grid32 IDのcallbackは部分scope受け入れに含めず、p216 wholeは未達。

## q1080-i01 結果（2026-10-14 06:37 JST）

Event ws074-p216-q1080-partial-clear-20261014: named export宣言5種を通常宣言のmodule scope/hoist/実行に接続。[証拠](evidence/20261014-q1080/README.md)。局所fixture exact、固定WPT共通support module単独評価exit0、classic JS14/14、build warning0、Acid3差0・Acid2差1865。q1080-i01 partial cleared、p216 wholeはmodule namespace/import/HTML未実装でuncleared。固定Grid32 no_outputとp189/p173全件100%未達。共有投影/GitHub publication保留。

## q1081-i01 有限部分範囲（2026-10-14 06:38 JST）

このchatのユーザーによる全3,805件100%までの有限Queue事前承認を最大90分（08:08 JSTまで）に適用。現行JS compiler/VMのclosure/environment/GC root、page script loaderとURL/fetch経路を読み、named import/exportのlive bindingとmodule graphを既存境界で実装する具体的内部API・依存順・有限次Queueへの分割設計を証拠化する。読み取り専用調査と必要な既存browser probeのみ。browser/runner/WPT/公開ABI/`plan/master.md`変更なし。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。p216 wholeと固定32 IDのcallbackはこの部分受け入れに含めない。

## q1081-i01 結果（2026-10-14 06:41 JST）

Event ws074-p216-q1081-partial-clear-20261014: [設計証拠](evidence/20261014-q1081/README.md)でstable export env slot、importの間接参照、graph link/evaluate、HTML parse後・DOMContentLoaded前のmodule queueを既存VM/page境界に写し、次の有限実装範囲を確定。q1081-i01 partial cleared。製品code/runner/WPT/ABI/Master変更なし。p216 wholeと固定Grid32 no_output、p189/p173全件100%は未達、共有投影/GitHub publication保留。
