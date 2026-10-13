<!-- awesome-plan project=zedbsd record=q1080 -->

# q1080 / WS074 module named export declarations

Status: finished（2026-10-14 06:36–06:37 JST）。Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を[p216有限部分scope](../ws074/phase216/phase.md)に適用。最大90分。

| Attempt | Phase | Exact scope | Outcome |
| --- | --- | --- | --- |
| q1080-i01 | [p216](../ws074/phase216/phase.md) partial | `export`付きfunction/class/var/let/const宣言をmodule scope/hoist/評価に接続。export list/default/re-export/import/HTML graphは未対応を維持。局所fixture・classic JS・build・Acidを検証。WPT/runner/ABI/Master不変更 | cleared。p216 whole uncleared |

Outcome: export宣言fixture exact、固定WPT共通support module単独評価exit0、classic JS14/14、warning0 build、Acid3差0・Acid2差1865。[証拠](../ws074/phase216/evidence/20261014-q1080/README.md)。

Remaining: export namespace、named import/module graph、HTML module load未対応。固定Grid32 no_output、p216 whole、p189全1,783件、p173全3,805件100%未達。共有Queue/Master/GitHub publication pending、pushなし。
