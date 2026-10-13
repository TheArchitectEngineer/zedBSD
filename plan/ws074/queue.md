<!-- awesome-plan project=zedbsd record=queue -->

# q1084 / WS074 module import metadata

Status: finished（2026-10-14 06:55–06:57 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（08:25 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1084-i01 | [ws074-p216](phase216/phase.md) partial | module instanceのstatic named importごとにspecifier/imported/local名を安定したread-only内部APIで列挙し、同一specifierの複数名と別specifierを区別できるようにする。linkは要求されたspecifierとexport名に一致するときだけ許し、誤った指定子を拒否。target URLとinstanceの対応は後続graphが管理。局所hostでmetadata、正しい/誤ったlink、既存live bindingを検証。URL fetch/cache/evaluation graph、HTML、namespace/default/re-exportは含めない。classic JS14、warning0 build、Acid3差0・Acid2非増加。WPT/runner/公開ABI/Master不変更 | cleared | p216 q1083 live import binding |

Dependency graph: q1083 live import binding → q1084 metadata/link identity → 後続URL graph/HTML → p189/q173再測定。

Outcome: read-only import列挙とspecifier照合を内部APIに追加し、2 module fixtureのlive readと複数specifier fixtureをPASS。[証拠](phase216/evidence/20261014-q1084/README.md)。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。q1084-i01 partial cleared、p216 whole uncleared。

Upcoming Work Outlook: module graphのURL解決・fetch・cache・依存評価順、続いてHTML module queueを有限Queueで実施。p216 wholeと全件100%は未達。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
