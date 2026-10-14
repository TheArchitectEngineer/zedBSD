<!-- awesome-plan project=zedbsd record=queue -->

# q1180 / WS074 horizontal Grid vertical-lr absolute text geometry

Status: finished（2026-10-14 13:33–13:38 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（15:03 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1180-i01 | [ws074-p220](phase220/phase.md) partial | positioned horizontal-tb LTR固定2×2 inline-grid、明示px tracks/gap0、親/子frame0、直接absolute vertical-lr/LTR auto-size ASCII子、両axis明示span1・left明示で、縦inline roomを確定row trackから取る。独立最小ページChromiumの子矩形一致、固定non-static007水平LTR先頭子のwidth10/height60とoffsetを測り、旧水平/縦positioned代表、host CSS/JS/Grid、warning0 build、Acid3差0/Acid2非増加を確認。他orthogonal/RTL/parent frame/auto line、WPT/runner/ABI/Master変更なし。WPT whole PASSは基準外 | cleared。[証拠](phase220/evidence/20261014-q1180/README.md) | q1179分類 + q1174 horizontal area |

Dependency graph: q1179分類 + q1174 horizontal area → q1180-i01。p220/p217/p189/p173 wholeは部分受け入れに含めない。

Upcoming Work Outlook: horizontal Grid vertical-rl子、縦Grid横子static area、auto lines、stretch005–016、RTL字形視覚、RTL行軸017、baseline、runner未対応。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
