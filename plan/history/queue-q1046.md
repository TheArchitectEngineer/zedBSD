<!-- awesome-plan project=zedbsd record=q1046 -->

# q1046 / WS074 top-level XHTML parsing

Status: finished（2026-10-14 02:04–02:07 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p196の有限部分scope](../ws074/phase196/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1046-i01 | [p196](../ws074/phase196/phase.md) partial | 最上位file/HTTP MIMEとscript-free XHTMLのXML DOM、HTML/XML host試験、固定10件/Acid回帰。WPT/Master/公開ABI変更なし | cleared（partial）。p196 whole uncleared |

Outcome: 最上位XHTMLを既存XML parser/projectionに接続し、固定timeout10件は同じ5秒条件で**0 timeout、1 local PASS/9 FAIL**。[証拠](../ws074/phase196/evidence/20261014-q1046/README.md)。HTTP/file XHTMLとHTML DOM試験4/4、host build warning0、XML DOM54/54・resource105/105・frame load52/52、Acid3差0/480000・Acid2差1865/120000。先行[p195](../ws074/phase195/phase.md)は後続証拠でwhole cleared、q1045-i01当時のunclearedは維持。

Remaining: 9件のflexbox pixel FAIL、script付き最上位XHTML、p190 runner coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避でpending。pushなし。
