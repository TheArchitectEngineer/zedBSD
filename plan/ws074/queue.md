<!-- awesome-plan project=zedbsd record=queue -->

# q1085 / WS074 module URL graph

Status: finished（2026-10-14 06:58–07:00 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（08:28 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1085-i01 | [ws074-p216](phase216/phase.md) partial | 内部module graphを追加。hostが与えるcanonical URL+source取得callbackを使い、各specifierをimport元URL基準で解決、URL cacheで1回instantiate、全importをlink後に依存先→import元順で1回評価する。共有依存とcycleで再帰を止め、失敗を返す。局所hostで相対URL/共有依存/評価順/一度限り/不正import/cycleを検証。HTML loader・HTTP/MIME・namespace/default/re-exportは含めない。classic JS14、warning0 build、Acid3差0・Acid2非増加。WPT/runner/公開ABI/Master不変更 | cleared | p216 q1084 import metadata |

Dependency graph: q1084 metadata → q1085 graph → 後続HTML loader → p189/q173再測定。

Outcome: canonical URL cacheとdependency DFS link/evaluateを内部graphに追加。局所hostで相対URL・共有依存一度限り・評価順・missing export・cycleをPASS。[証拠](phase216/evidence/20261014-q1085/README.md)。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。q1085-i01 partial cleared、p216 whole uncleared。

Upcoming Work Outlook: HTML module script queueからgraphを呼び、固定Grid32を再測定。p216 wholeと全件100%は未達。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
