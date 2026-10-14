<!-- awesome-plan project=zedbsd record=queue -->

# q1135 / WS074 Grid recensus regression repair and animation diagnosis

Status: finished（2026-10-14 10:26–10:29 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（11:56 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1135-i01 | [ws074-p223](phase223/phase.md) partial | 空Gridに実在しない暗黙列をintrinsic幅として算入しない最小layout修正。最小empty/explicit/absolute/子ありgeometry、固定empty-grid WPTと旧PASS代表、CSS/JS/Grid host、warning0 build、Acid3差0/Acid2非増加。Grid animation・track grammar、WPT/runner/ABI/Master変更なし | cleared（[証拠](phase223/evidence/20261014-q1135/README.md)）。p223 whole uncleared | q1134 empty-grid旧PASS後退 |
| q1135-i02 | [ws074-p224](phase224/phase.md) partial | 固定行track animation2件のraw callback・WPT sourceとCSS/JS animation/`getComputedStyle`経路を読み、旧PASSの有効性と一般欠陥・有限実装境界を記録。読取/probe/runner実行のみ、製品code/WPT/runner/ABI/Master変更なし | cleared（[証拠](phase224/evidence/20261014-q1135/README.md)）。p224 whole uncleared | q1134 animation旧PASS後退。i01から独立 |

Dependency graph: q1134固定Grid502 → q1135-i01 と q1135-i02（相互独立）。p223/p224/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: p224の一般animation機構、alignment192、grid-definition42、parsing31などの一般欠陥群。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
