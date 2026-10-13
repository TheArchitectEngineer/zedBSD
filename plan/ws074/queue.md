<!-- awesome-plan project=zedbsd record=queue -->

# q1089 / WS074 definite abspos Grid static origin

Status: finished（2026-10-14 07:35–07:39 JST）
Owner: この Codex セッション単独。`plan/master.md` は変更しない。
Approval: このchatのユーザー指示「Interop 2025の100%クリアまで、自走してください。ququeはあらかじめ承認します。」を下記有限範囲に適用。
Timebox: 最大90分（09:05 JSTまで）。

| Attempt | Phase | Exact scope | State | Dependencies |
| --- | --- | --- | --- | --- |
| q1089-i01 | [ws074-p217](phase217/phase.md) partial | 位置指定のある水平LTR grid containerの直接absolute子について、両axisのinsetがauto、行・列が既存の明示track内の有効なlineで確定する場合、track size/gap/justify-contentからstatic area原点を算出する。out-of-flow子はtrack sizing/auto placementへ入れず、非対象のstatic位置と明示insetを保つ。最小HTML、固定WPT水平代表、既存host JS14/警告0 build/Acid3差0・Acid2非増加を検証。RTL/縦書き、auto/無効line、full self-alignment、Grid502全再測定、WPT/runner/公開ABI/Master変更なし | cleared | p217 q1088 diagnosis |

Dependency graph: q1088 diagnosis → q1089 static area origin → 後続RTL/縦書き/align修正とp189再測定。

Outcome: 水平LTRの有効な明示area原点をstatic_x/yへ反映。最小hostで子(108,88)、明示inset(15,17)、無効line(8,8)を確認。固定WPT代表2件とも水平subtest FAIL→PASS、wholeはRTL/縦書きでFAIL継続。旧PASS module代表維持。[証拠](phase217/evidence/20261014-q1089/README.md)。classic JS14/14、warning0 build、Acid3差0・Acid2差1865。q1089-i01 partial cleared、p217 whole uncleared。

Upcoming Work Outlook: q1089対象の固定WPT遷移を確認し、残るalignment系列とGrid全502再測定へ。共有 `plan/queue.md` / Master / GitHub publication はQ1との競合を避けpending projection。
