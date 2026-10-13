<!-- awesome-plan project=zedbsd record=q1054 -->

# q1054 / WS074 XML external-ID DOCTYPE subset

Status: finished（2026-10-14 02:43–02:47 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p203の有限部分scope](../ws074/phase203/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1054-i01 | [p203](../ws074/phase203/phase.md) partial | XML外部ID DOCTYPEを取得副作用なしで構文受理、内部subset/未知実体を拒否。q1053 error359 ID・固定Flex10件とhost/XML/build/Acid回帰。WPT/Master/公開ABI変更なし | cleared（partial）。p203 whole uncleared |

Outcome: 359旧error IDの312件でload error解消、122 local PASS/190 pixel FAIL/47 load error。残り47件は全てXHTML1 DTD由来の`&nbsp;`未解決。[証拠](../ws074/phase203/evidence/20261014-q1054/README.md)。host XML243/243・DOM54/54・document16/16・binding34/34、warning0 build、固定Flex10/10 pixel PASS、Acid3 score100/100・pixel差0、Acid2差1865。

Remaining: XHTML外部DTD由来`nbsp`47件、p190全reftest coverage、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1との競合回避でpending。pushなし。
