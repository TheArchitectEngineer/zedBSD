<!-- awesome-plan project=zedbsd record=q1083 -->

# q1083 / WS074 named import live binding

Status: finished（2026-10-14 06:49–06:54 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p216有限部分scope](../ws074/phase216/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1083-i01 | [p216](../ws074/phase216/phase.md) partial | named static importをimmutable indirect bindingとして登録し、内部link APIでexport instanceへ接続。link前拒否、live値/TDZ/代入拒否/GC/重複を2-module host検証。URL graph/HTML/namespace/default/re-exportなし。classic JS/build/Acid回帰。WPT/runner/公開ABI/Master変更なし | cleared。p216 whole uncleared |

Outcome: exporter解放・強制GC後もimporterからcounter1→2をlive読取、link/TDZ/代入/重複拒否PASS。[証拠](../ws074/phase216/evidence/20261014-q1083/README.md)。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。q1081設計のcompile-time env constant案はlate linkに合わせVM indirect bindingへ改訂。

Remaining: URL graph fetch/cache/評価順、HTML module load未実装。固定Grid32 no_output、p216 whole、p189全1,783件、p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
