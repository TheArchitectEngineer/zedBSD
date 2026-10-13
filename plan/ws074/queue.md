<!-- awesome-plan project=zedbsd record=queue -->

# q1083 / WS074 named import live binding

Status: finished（2026-10-14 06:49–06:54 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 2026-10-14 06:49 JST開始、最大90分（08:19 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1083-i01 | [ws074-p216](phase216/phase.md) partial | named static importをmodule local immutable indirect bindingとして登録し、既存export instanceへ内部link APIで接続。link前評価拒否、import readのlive値/TDZ/代入拒否、GC参照、重複/未解決名を局所2-module hostで検証する。VM内部参照cell/opcodeは必要最小限。URL graph fetch・HTML loader・namespace/default/re-exportは含めない。classic JS14、warning0 build、Acid3差0・Acid2非増加。WPT/runner/公開ABI/Master不変更 | cleared | p216 q1082 stable export env |

Dependency graph: p216 q1081設計 → q1082 stable export env → q1083 live import link → 後続URL graph/HTML Queue → p189/q173再測定。

Outcome: 2-module hostでunlinked/TDZ/代入/重複拒否、target解放+強制GC後のlive counter1→2をPASS。[証拠](phase216/evidence/20261014-q1083/README.md)。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。q1083-i01 partial cleared、p216 whole uncleared。

Upcoming Work Outlook: URL graph fetch/評価順とHTML loadを別有限Queueで進める。p216 whole、p189/p190/p173全件100%は未達。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
