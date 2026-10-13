<!-- awesome-plan project=zedbsd record=q1055 -->

# q1055 / WS074 XHTML 1.0 Strict nbsp subset

Status: finished（2026-10-14 02:48–02:51 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p204の有限scope](../ws074/phase204/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1055-i01 | [p204](../ws074/phase204/phase.md) whole | XHTML1 Strict公開DTD識別子の`nbsp`だけをU+00A0へ展開、別DTD/未知実体拒否。固定47 ID・Flex10件、host/XML/build/Acid回帰。WPT/Master/公開ABI変更なし | cleared。p204 whole cleared、p203も後続証拠でcleared |

Outcome: 旧47 load errorは0、0 local PASS/47 pixel FAILに進行。host XML249/249・DOM54/54・document16/16・binding34/34、warning0 build、固定Flex10/10 pixel PASS、Acid3 score100/100・pixel差0、Acid2差1865。[証拠](../ws074/phase204/evidence/20261014-q1055/README.md)。q1054の312成功と合わせて旧359 DOCTYPE load errorの解消を確認。q1054当時のpartial outcomeは保持。

Remaining: 47 writing-mode pixel FAILを含むp190全reftest coverageとp173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
