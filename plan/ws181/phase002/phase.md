<!-- awesome-plan project=zedbsd record=ws181-p002 -->
# ws181-p002: 窓の状態 — docked から floating で全部の窓を floating に、docking の隠れと最小化を分ける、docked の窓を閉じたら floating

Parent: [WS181](../ws.md)
Status: test-wait（T1-342）。旧: in-progress（2026-10-07 q842-i01 P2: 実装・build・host 試験まで。QEMU は WS181 の区切りで T1 へ）
Disposition: normal
Queue: q842 / q842-i01
Design: [p001 design.md](../phase001/design.md) §1

## 範囲

design.md §1（目標 1・2）。`layout.c` の純関数（`kwl_layout_state`・`kwl_layout_leave_action`・`kwl_layout_owner_check`）、`shell.c` の
`layout_leave`・`window_float_quiet`・`layout_follow`（持ち主の観測、手順 A・B）・`kwl_glass_forget`、`layout_press_switches` と
`layout_keep_front` の削除、`window_undock` から `layout_set` を外す、`kwl.h` の `restore_default`・`dock_owner[]`・`dock_owner_gone[]`、
`objects.c` の消滅の知らせ。試験の追従（ws142 の p010-guest.sh・scenario）。

## 受け入れ

- docked の窓を floating にすると、後ろの docked の窓も全部 floating（`KWL LAYOUT leave … quiet=n`、`windows … docked=0`）。
- 窓の mode で窓を click しても大きさが変わらない（`layout_press_switches` が無い）。
- 表示中の desktop の docked の窓を閉じる・最小化・見えない desktop へ送ると docked mode を出る（`leave via=closed|minimized|moved`）。隠れていた窓は floating で見え、最小化の窓は最小化のまま。次の窓は dock されない。
- 見えない desktop の docked の窓が閉じても表示は変わらない。連れて行く移動は docked のまま。
- build（zedBSD と Linux の Keiland）warning 0、host 試験 PASS。QEMU（T1）は design.md §1.5 の 1〜7。

## 記録

- 2026-10-07 P2: 実装した。commit a545bc518（layout.c・layout.h・host 試験）、7ffe6036f・015015a7b（shell.c・kwl.h・objects.c）、1367fa445（試験と ws142 の記録の追従）。
- 実装で分かった設計の直し（design.md に反映、cf91e527c）: `window_to_desktop()` は `layout_follow()` を直に呼ばない（Ctrl+Alt+Shift+矢印は移動の後に desktop を回すので、間に確かめると誤る）。`fullscreen_docked` は leave で消さない（windowed で全画面を出ると protocol.c が floating の場所へ戻す）。前の窓が dialog の時は、親が docked の時だけ親を持ち主にし、floating の親は dock しない（今の `layout_keep_front` と同じ振る舞いを保つ）。

## 確認

| 確認 | 結果 |
| --- | --- |
| host `plan/ws181/tests/run-host-layout-state.sh` | PASS（checks=43 failures=0） |
| host `plan/ws142/tests/run-host-layout.sh`（ws142 の回帰、layout.c の既存の規則） | PASS（57 checks） |
| zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`） | 成功、warning 0（-Werror） |
| Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux`） | 成功、warning 0 |
| `sh -n plan/ws142/tests/p010-guest.sh` | 構文 OK（実行は T1） |
| QEMU（design.md §1.5 の 1〜7、p010-guest.sh の 1〜5） | 未実施（T1 に依頼する） |

## 残り

- QEMU の確認（T1）。
- 正常系の外（backlog-p2.md へ）: なし（今の所）。
