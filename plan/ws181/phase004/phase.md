<!-- awesome-plan project=zedbsd record=ws181-p004 -->
# ws181-p004: 整列のメニューと整列モード

Parent: [WS181](../ws.md)
Status: in-progress（2026-10-07 q842-i01 P2: 実装・build・host 試験まで。QEMU は T1 へ依頼（WS181 の区切り）、test-wait にするのは Q1）
Disposition: normal
Queue: q842 / q842-i01
Design: [p001 design.md](../phase001/design.md) §4・§5

## 範囲

design.md §4・§5（目標 5・6）と、2026-10-07 のユーザーの回答（D5 pill のどこでも、S6 閉じたら終える、D7 上限を超える窓はその場に）。

- `arrange.c/h`（純関数）: 5 つの形の枠（`kwl_arrange_slots`、四方の余白 8・隙間 8、余りは最後の枠）、割り当て（`kwl_arrange_assign`、中心の距離の 2 乗の和が最小の部分集合 DP）。
- `arrange-shell.c`: bar の desktop の pill の press の release でメニュー（上段に desktop の絵 4 つ、下に整列の 5 行、行の図は枠の縮小、↑↓・Enter・Esc）、適用（docked なら静かに leave、上から上限まで、枠へ 180 ms の glide、整列の窓を前へ）、整列モード（title の drag の 4 つの経路で入れ替え、bar で離すと dock、それ以外は戻る）、終わり（`layout_set(DOCKED)`・閉じる・unmap・最小化・全画面・別の desktop・縁の大きさの変更・新しい窓）、pill の印（形の図）。状態は file の static（設計の `server->arrange[]`・`server->swap` の代わり）。
- shell.c の口: `kwl_glass_desktops_pill`・`kwl_glass_desktop_turn`・`kwl_glass_work_area`（keyboard の落ち着いた値、下端の帯の上まで）・`kwl_glass_leave_quiet`・`kwl_glass_place_body`・`kwl_glass_body`・`kwl_glass_dock_window`、`body_rect` の glide、`bar_press` の desktop の絵の切り替えを外した（メニューの中へ）。seat.c に鍵盤。
- 翻訳: `userland/desktop/locale/wayland.keys` と `ja/wayland.tr` に 5 つの形の名前。
- 試験の追従: `plan/ws035/tests/zdesktop-p065.sh`（pill の click → メニューの desktop 2、`via=menu`。ws099-p034 が参照）。

## 受け入れ

- pill のどこを tap・click してもメニューが開き、5 つの形で窓が枠に入る（`KWL ARRANGE apply layout=… windows=n slots=…`）。
- 整列モードで title の drag が入れ替え（`KWL ARRANGE swap`）、double click・bar への drag で dock して終わり（`KWL ARRANGE end reason=dock`）、戻すと普通の floating（title の drag は `KWL GLASS moved`）。
- メニューの desktop の絵で切り替え（`KWL GLASS desktop=n via=menu`）。
- build（zedBSD・Linux の Keiland）warning 0、host 試験 PASS。

## 記録

- 2026-10-07 P2: ba36a7e3c（arrange.c/h・arrange-shell.c・shell.c・seat.c・kwl.h・glass.h・Makefile 3 つ・翻訳・host 試験・zdesktop-p065.sh）、52c7c378a（QEMU の試験 `plan/ws181/tests/ws181-guest.sh`、WS181 全体の A1〜C12）、4ab495ae4（AAT の scenario の draft `tests/scenarios/desktop/windows/arrange.md`）、ce31f69b1（正常系の外を plan/ws177/backlog-p2.md へ）。

## 確認

| 確認 | 結果 |
| --- | --- |
| host `plan/ws181/tests/run-host-arrange.sh` | PASS（checks=1004 failures=0: 5 形 × n、余白・重なり・隙間・埋まり、形、割り当ての順・隅、総当たりと同じ最小） |
| zedBSD の compositor（`make ZEDBSD_CONFIG=plan/ws035/tests/config-amd64-zdesktop.mk BUILD=build/ws181 build/ws181/bin/wayland`） | 成功、warning 0 |
| Linux の Keiland（`make keiland-linux KEILAND_LINUX_BUILD=build/ws181-linux`） | 成功、warning 0 |
| `python3 tools/i18n/tr.py check userland/desktop/locale/ja/wayland.tr` | 0 problems |
| `sh -n` ws181-guest.sh・zdesktop-p065.sh | 構文 OK（実行は T1） |
| QEMU（ws181-guest.sh の C10〜C12、zdesktop-p065.sh） | 未実施（T1） |

## 残り

- QEMU の確認（T1）。全文規約の見直しは後回し（2026-10-06 夜 Q1）。
