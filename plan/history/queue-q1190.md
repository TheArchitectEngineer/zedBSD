<!-- awesome-plan project=zedbsd record=q1190 -->

# q1190 / WS074 remaining Grid non-static positioned diagnosis

Status: finished（2026-10-14 13:57 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p217有限部分scope](../ws074/phase217/phase.md)に適用。最大90分。

| Attempt | Phase | Scope | Outcome |
| --- | --- | --- | --- |
| q1190-i01 | [p217](../ws074/phase217/phase.md) partial | 固定non-static009–012 raw callback/原本/最大3 layout経路による初回失敗分類 | cleared。009–011は横親Gridのframed縦absolute子の縦inline経路拒否、012は親frame/auto insetの追加static-position不一致 |

Outcome: 009/010先頭縦子の幅40期待/90実測、011は26/76、いずれも+50。012は同じ幅差に加え縦Grid先頭子offset不一致。[証拠](../ws074/phase217/evidence/20261014-q1190/README.md)。読取のみ、browser/runner/WPT/ABI/Master無変更。

Resume: unframed水平Gridのborder付き縦absolute子009を独立最小ページで切り分ける。p217/p189/p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
