<!-- awesome-plan project=zedbsd record=queue -->

# q1190 / WS074 remaining Grid non-static positioned diagnosis

Status: finished（2026-10-14 13:57 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:27 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1190-i01 | [ws074-p217](phase217/phase.md) partial | 固定Grid rawのnon-static-positioned009–012計4件の初回失敗をcallback・寸法/offset・親子writing-mode/direction・frame・grid line/inset別に分類し、WPT原本と該当layout経路を最大3種類調べる。次の有限実装候補と非対象回帰を記録。読取・証拠のみ、browser/runner/WPT/ABI/Master変更なし。未達ならuncleared | cleared（[証拠](phase217/evidence/20261014-q1190/README.md)） | q1189 Grid502 recensus |

Dependency graph: q1189 Grid502 recensus → q1190-i01。p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: non-static008–012 frame variants、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
