<!-- awesome-plan project=zedbsd record=queue -->

# q1080 / WS074 module named export declarations

Status: finished（2026-10-14 06:36–06:37 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 2026-10-14 06:36 JST開始、最大90分（08:06 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1080-i01 | [ws074-p216](phase216/phase.md) partial | JS module内の`export`付きfunction/class/var/let/const宣言を通常宣言と同じscope/hoist/実行へ接続し、module内参照可能・global不可視を確認する。`export {}`、default、re-export、import/module graph/HTML loaderは含めず未対応を明示維持。局所fixture、classic JS14件、warning0 host build、Acid3差0・Acid2非増加を確認。WPT/runner/ABI/Master不変更 | cleared | p216 q1079 module scope |

Dependency graph: p173 manifest → p216 q1079 scope → p216 q1080 export宣言 → 後続module graph/HTML Queue → p189/q173再測定。

Outcome: export宣言fixture exact・固定WPT共通support module単独評価exit0、classic JS14/14、warning0 build、Acid3差0・Acid2差1865。[証拠](phase216/evidence/20261014-q1080/README.md)。q1080-i01 partial cleared、p216 whole uncleared。

Upcoming Work Outlook: module namespace/graphとHTML loadを別有限Queueへ進める。p216 whole、p189/p190/p173全件100%は未達。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
