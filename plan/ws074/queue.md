<!-- awesome-plan project=zedbsd record=queue -->

# q1079 / WS074 standalone module scope

Status: finished（2026-10-14 06:32–06:35 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 2026-10-14 06:32 JST開始、最大90分（08:02 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1079-i01 | [ws074-p216](phase216/phase.md) partial | JS module単独実行でtop-level var/function/class/let/constがglobalへ漏れず、module内とclosureから参照可能なbindingにする。strict top-level thisも確認。既存`--js --module`とJS compiler/scopeの最小内部変更、局所host fixture、warning0 build、classic JS回帰、固定Acid3差0・Acid2非増加を検証。HTML module loader/import/export graph、WPT/runner/公開ABI/Masterは変更しない | cleared | p189 q1078 module調査 |

Dependency graph: p173 manifest → p189 q1078 module調査 → p216 q1079 scope → 後続module graph/HTML Queue → p189/q173再測定。

Outcome: module scope局所fixture exact、classic JS14/14、warning0 build、Acid3差0・Acid2差1865。[証拠](phase216/evidence/20261014-q1079/README.md)。q1079-i01 partial cleared、p216 whole uncleared。

Upcoming Work Outlook: named import/exportとHTML loadを別有限Queueへ進める。p216 whole、p189/p190/p173全件100%は未達。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
