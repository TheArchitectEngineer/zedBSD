<!-- awesome-plan project=zedbsd record=q1042 -->

# q1042 / WS074 manifest-driven static reftest selection

Status: finished（2026-10-14 01:39–01:41 JST）。Approval: この chat のユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を [p190の有限部分scope](../ws074/phase190/phase.md) に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1042-i01 | [p190](../ws074/phase190/phase.md) partial | static runnerのmanifest直読/未実行ID理由、重複/variant control、先行42件同一性、全分野100件sample。browser/WPT/Master/公開ABI変更なし | cleared（partial）。p190 whole uncleared |

Outcome: 固定reftest1,974 IDは静的適格1,561、script402、scripted reference5、variant6。[証拠](../ws074/phase190/evidence/20261014-q1042/README.md)。選択3/3+chain4/4、details/anchor既存42件の結果・画素差一致、全分野100件sample28 local PASS/72 FAIL。Python compile/ID分母/diff check PASS。browser/WPT/Master無変更のためhost build/Acid再計測省略。

Remaining: 静的適格1,461件の未sample分、script関連407・variant6のrunner条件、描画FAIL、p173全3,805件100%。共有Queue/Master/GitHub publicationはQ1と競合回避のためpending。pushなし。
