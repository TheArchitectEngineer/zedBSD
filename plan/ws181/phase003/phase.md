<!-- awesome-plan project=zedbsd record=ws181-p003 -->
# ws181-p003: App Home の独立のモードと画面の端の gesture

Parent: [WS181](../ws.md)
Status: in-progress（2026-10-07 q842-i01 P2: 実装・build・host 試験まで。QEMU は WS181 の区切り（p004 の後）で T1 へ）
Disposition: normal
Queue: q842 / q842-i01
Design: [p001 design.md](../phase001/design.md) §2・§3

## 範囲

design.md §2・§3（目標 3・4）。home.c の層の上への動き・覗きの削除・下端の swipe で Home・Home の上の下向きの drag で閉じる・
`kwl_home_close_now`、shell.c の上端の帯（touch だけ、保持と流し直し、全画面の上は流し直さない）・Wiseview の上端からの gesture
（`wiseview_top`）・Wiseview の下端から Home・pull の距離（どの向きでも 48 px）と `drag_left_bar`・CSD の docked の窓の move の要求、
Home を overview から外す。純関数は新しい `edge.c`（`kwl_edge_classify`・`kwl_edge_band_motion`・`kwl_edge_drag_axis`・`kwl_edge_distance`）。

## 受け入れ

- 下端から上の swipe で Home が開き（`KWL HOME open via=edge`）、desktop の層は上へ出ていき、開いた Home に desktop は見えない。
- Home の上の下向きの drag で閉じる（`KWL HOME close via=pull-down`）、下端から上は何もしない。
- touch の上端の帯から下で Wiseview（`KWL WISEVIEW gesture via=top-edge`）。帯の tap は bar の widget に届く（`KWL EDGE band replay release=1` の後に今と同じ log）。mouse は今どおり。
- docked の title をどの向きに 48 px 引いても外れ、そのまま移動、bar の中で離しても dock しない（bar の外へ出てからは dock）。
- build（zedBSD・Linux の Keiland）warning 0、host 試験 PASS。

## 記録

- 2026-10-07 P2: 436594e02（home.c の層・下端の swipe・Home の下向きの drag・Wiseview の下端・Home を overview から外す）、a9bca2f9c（edge.c/h と host 試験、上端の帯、`wiseview_top`、pull の距離と `drag_left_bar`、CSD の move の要求、Makefile・Makefile.linux・Makefile.freebsd に edge.c）。
- 古い試験（ユーザーの 2026-10-06 の整理の基準）: Home の覗きの角・Home の上の下端の swipe を期待する `plan/ws035/tests/zdesktop-p069.sh`（`KWL HOME close via=corner`）、`plan/ws079/tests/zdesktop-p010.sh`（`HOME bottom swipe`・`close via=bottom`・`close via=corner`）、`plan/ws079/tests/zdesktop-p013-touch.sh`（`HOME bottom swipe`・`close via=bottom`）は、master の Tools・未完了の Phase・tests/ のシナリオ・T1 の未実行の依頼のどれからも参照されていない（cleared の Phase と completed の WS の記録だけ）→ 直さず削除を Q1 に依頼した。

## 確認

| 確認 | 結果 |
| --- | --- |
| host `plan/ws181/tests/run-host-edge.sh` | PASS（checks=29 failures=0） |
| zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux`） | 成功、warning 0 |
| `plan/tools/keiland-linux/makefile-sync.sh` | wayland の不一致なし（preview の既存の FAIL は WS181 と無関係） |
| QEMU（design.md §3.3 の 1〜5） | 未実施（T1） |

## 残り

- QEMU の確認（T1）。
- 正常系の外（backlog-p2.md）: 帯の中の長押し（apps bar の preview）、全画面の app への帯の press の流し直し。
