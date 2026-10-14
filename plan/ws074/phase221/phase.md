<!-- awesome-plan project=zedbsd record=ws074-p221 -->

# ws074-p221: Interop 2025 Grid parsing and CSSOM

Status: in-progress（q1123部分診断）
Disposition: normal
Parent: [WS074](../ws.md)
Primary Milestone: MG006（WS074を継承）
Prerequisites: [p189 q1105固定Grid502](../phase189/evidence/20261014-q1105/README.md)。Grid shorthandの既存出力は[p218](../phase218/phase.md)から利用する。独立した解析群は別々に進められる。

## 目的と受け入れ

固定Interop 2025 Grid testharnessの`/css/css-grid/parsing/`41件を現行browserで再測定し、宣言構文・shorthand・computed CSSOMの一般欠陥を原因別に修正する。41件全whole PASSと旧PASS維持を実測した後にwhole clearanceを判定する。p189全1,783件/p173全3,805件100%は別受け入れ。

## 設計・境界

CSS values/parser/cascade、Grid style、必要なCSSOM、および`plan/ws074/tests/`を各有限Queueのscopeで扱う。WPT本体/runner採点・公開ABI・`plan/master.md`は変更しない。[Guardrail](../../guardrail.md)、[C全文](../../coding-style.md)、[browser component全文](../../standards/browser-component.md)を適用。code後はWS074 near-final full-standard conformance Phaseで再確認する。

Event ws074-p221-addition-20261014: [p189 q1105](../phase189/evidence/20261014-q1105/README.md)の固定Grid502残392 whole FAILに`parsing/`35件を含む。p217のalignment/p220の縦文字と独立する解析・CSSOM群として追加。p189/p173分母・受け入れ不変。共有投影/GitHub publication保留。

## q1123-i01 有限部分範囲（2026-10-14 09:36 JST）

このchatの全3,805件100%までの有限Queue事前承認を最大90分（11:06 JSTまで）に適用。固定manifestの`/css/css-grid/parsing/` testharness41件をp189と同じWPT/フォント/HTTP/settle/process条件で現行browser再測定。whole/subtestのPASS/FAIL・初回失敗・sourceの構文を分類し、最大3の共通原因と最初の有限実装候補を証拠化する。読取/runner実行のみ。browser製品source/WPT/runner/公開ABI/`plan/master.md`/Acid変更なし。p221 whole/p189/p173全件100%は本部分基準外。

## q1123-i01 結果（2026-10-14 09:38 JST）

Event ws074-p221-q1123-partial-clear-20261014: [証拠](evidence/20261014-q1123/README.md)。固定Grid parsing41件を同条件再測定し6 whole PASS/35 FAIL、subtest336 PASS/930 FAIL、no_output/process_error0。失敗はGrid auto/areas属性欠落9、track解析/直列化14、area/shorthand10、その他2。最初の有限修正は既にparsed `grid-template-rows`のCSSOM返却。q1123部分cleared、p221/p189/p173全件100%未達。browser/WPT/runner/ABI/Master/Acid無変更、共有投影/GitHub publication保留。
